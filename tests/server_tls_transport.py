"""Opt-in CSSLSocket/Windows TLS adapter probes; loopback and isolated trust."""
from pathlib import Path
import datetime
import os
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time


def main():
    exe, library, work = map(Path, sys.argv[1:])
    if os.environ.get('NOVA_REAL_TESTS') != '1' or not library.is_file():
        print('SKIP original TLS: NOVA_REAL_TESTS=1 and local library required')
        return 77
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import rsa
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID
    work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=work) as directory:
        root = Path(directory)
        now = datetime.datetime.now(datetime.timezone.utc)
        key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'SV09 synthetic root')])
        ca = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key())
              .serial_number(x509.random_serial_number()).not_valid_before(now - datetime.timedelta(days=2))
              .not_valid_after(now + datetime.timedelta(days=2))
              .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
              .sign(key, hashes.SHA256()))
        (root / 'root.der').write_bytes(ca.public_bytes(serialization.Encoding.DER))
        server_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        (root / 'key.pem').write_bytes(server_key.private_bytes(serialization.Encoding.PEM,
            serialization.PrivateFormat.TraditionalOpenSSL, serialization.NoEncryption()))
        for expired in (False, True):
            cert = (x509.CertificateBuilder()
                    .subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'server.test')]))
                    .issuer_name(name).public_key(server_key.public_key()).serial_number(x509.random_serial_number())
                    .not_valid_before(now - datetime.timedelta(days=2))
                    .not_valid_after(now + datetime.timedelta(days=-1 if expired else 1))
                    .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
                    .add_extension(x509.SubjectAlternativeName([x509.DNSName('server.test')]), critical=False)
                    .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
                    .sign(key, hashes.SHA256()))
            (root / f'leaf-{expired}.pem').write_bytes(cert.public_bytes(serialization.Encoding.PEM) +
                                                    ca.public_bytes(serialization.Encoding.PEM))
        cases = [
            ('127.0.0.1', False, False, True, False),  # Explicit IP retains its existing policy.
            ('127.1.1.2', False, False, False, False), # Exception cannot leak to another peer.
            ('server.test', False, False, False, False), # DNS resolution cannot bypass trust.
            ('server.test', True, False, True, False),
            ('wrong.test', True, False, False, False),
            ('server.test', True, True, False, False),
            ('server.test', True, False, True, True),  # Bare EOF is not close_notify.
        ]
        for address, trust, expired, accepted, abrupt in cases:
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            modern = address != '127.1.1.2'
            context.minimum_version = ssl.TLSVersion.TLSv1_2 if modern else ssl.TLSVersion.TLSv1
            context.maximum_version = ssl.TLSVersion.TLSv1_2
            if not modern:
                context.set_ciphers('AES128-SHA:@SECLEVEL=0')
            context.load_cert_chain(root / f'leaf-{expired}.pem', root / 'key.pem')
            names = []
            context.set_servername_callback(lambda _socket, name, _context: names.append(name))
            errors = []
            with socket.socket() as listener, socket.socket() as lobby_listener, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as game:
                listener.bind(('127.0.0.1', 0))
                listener.listen()
                listener.settimeout(16)
                if accepted:
                    lobby_listener.bind(('127.0.0.1', 0))
                    lobby_listener.listen()
                    lobby_listener.settimeout(16)
                    game.bind(('127.0.0.1', 0))
                    game.settimeout(10)
                def serve():
                    try:
                        connection, _ = listener.accept()
                        with connection:
                            connection.settimeout(16)
                            try:
                                with context.wrap_socket(connection, server_side=True) as secured:
                                    if accepted:
                                        expected = bytes(i % 251 for i in range(131071))
                                        received = bytearray()
                                        while len(received) < len(expected):
                                            chunk = secured.recv(min(4093, len(expected) - len(received)))
                                            if not chunk:
                                                raise AssertionError('TLS stream truncated')
                                            received.extend(chunk)
                                        if received != expected or secured.version() != 'TLSv1.2':
                                            raise AssertionError('TLS version or application payload mismatch')
                                        header = b'HTTP/1.1 200 OK\r\nX-Nova-Account: ' + b'a' * 64 + b'\r\nX-Nova-Session: ' + b'b' * 64 + b'\r\n\r\n'
                                        response = header + expected
                                        for offset in range(0, len(response), 3113):
                                            secured.sendall(response[offset:offset + 3113])
                                        if not abrupt:
                                            secured.unwrap().close()
                                    else:
                                        secured.recv(1)
                            except ssl.SSLError:
                                pass  # Expected when the original verifier rejects the root.
                        if accepted:
                            connection, _ = lobby_listener.accept()
                            with connection:
                                connection.settimeout(10)
                                with context.wrap_socket(connection, server_side=True) as lobby:
                                    def exact(size):
                                        result = bytearray()
                                        while len(result) < size:
                                            part = lobby.recv(size - len(result))
                                            if not part:
                                                raise AssertionError('lobby stream truncated')
                                            result.extend(part)
                                        return bytes(result)
                                    if exact(73) != b'NV3L\x01' + b'b' * 64 + b'\0\0\0\x01':
                                        raise AssertionError('lobby authentication/capability preface')
                                    frame = bytes.fromhex('000a01020000000100000000')
                                    if exact(len(frame)) != frame:
                                        raise AssertionError('native lobby frame changed')
                                    def control(body):
                                        return (len(body) + 6).to_bytes(2, 'big') + b'\x01\x02N3PC' + body
                                    ticket_id = bytes(range(16))
                                    client_key, server_key = bytes(range(32)), bytes(range(32, 64))
                                    grant = (b'\x01\x01' + socket.inet_aton('127.0.0.1') + game.getsockname()[1].to_bytes(2, 'big') +
                                             b'\0\0\0\x01' + ticket_id + client_key + server_key)
                                    records = control(grant) + frame
                                    for offset in range(0, len(records), 7):
                                        lobby.sendall(records[offset:offset + 7])
                                    packet, source = game.recvfrom(65535)
                                    header = b'NV3\x01' + ticket_id + (1).to_bytes(8, 'big')
                                    if packet[:28] != header:
                                        raise AssertionError('UDP adapter header/counter')
                                    native = AESGCM(client_key).decrypt(bytes(4) + packet[20:28], packet[28:], packet[:28])
                                    if native != b'N3PC\x01\x01\x00\x0c' + frame:
                                        raise AssertionError('UDP adapter changed native payload')
                                    def protected(sequence, kind, body):
                                        h = b'NV3\x01' + ticket_id + sequence.to_bytes(8, 'big')
                                        plain = b'N3PC\x01' + bytes([kind]) + len(body).to_bytes(2, 'big') + body
                                        return h + AESGCM(server_key).encrypt(bytes(4) + h[20:28], plain, h)
                                    forged = bytearray(protected(1, 4, bytes.fromhex('010000000063')))
                                    forged[-1] ^= 1
                                    game.sendto(forged, source)
                                    game.sendto(protected(1, 4, bytes.fromhex('010000000001')), source)
                                    # Synthetic precise pose, then an explicit new life and stale
                                    # declaration: only authenticated life records own generations.
                                    pose = bytes.fromhex('01000000000100000001000000643f80000040000000404000004000ffff')
                                    game.sendto(protected(2, 2, pose), source)
                                    game.sendto(protected(3, 4, bytes.fromhex('010000000002')), source)
                                    game.sendto(protected(4, 4, bytes.fromhex('010000000001')), source)
                                    game.sendto(protected(5, 2, pose), source)
                                    # Readiness must consume internal traffic without
                                    # advertising a native recvfrom that yields EAGAIN.
                                    time.sleep(0.05)
                                    game.sendto(protected(6, 1, frame), source)
                                    proposal, sender = game.recvfrom(65535)
                                    if sender != source or proposal[:20] != header[:20] or int.from_bytes(proposal[20:28], 'big') != 2:
                                        raise AssertionError('snapshot changed native descriptor/ticket/counter')
                                    pose = AESGCM(client_key).decrypt(bytes(4) + proposal[20:28], proposal[28:], proposal[:28])
                                    if pose != bytes.fromhex('4e3350430102001e01000000000200000001000000643fa00000c020000040700000ffe9ffe9'):
                                        raise AssertionError(f'precise upstream pose: {pose.hex()}')
                                    # Match teardown can contain only an internal revoke.
                                    # Do not provide native bytes until the client proves
                                    # its blocking lobby poll filtered that record.
                                    lobby.sendall(control(b'\x01\x02' + ticket_id))
                                    if exact(len(frame)) != frame:
                                        raise AssertionError('control-only poll acknowledgement')
                                    lobby.sendall(frame)
                                    if exact(len(frame)) != frame:
                                        raise AssertionError('post-revocation lobby acknowledgement')
                            for _service_port in (80, 20001, listener.getsockname()[1], 443, 20001):
                                connection, _ = listener.accept()
                                with connection:
                                    connection.settimeout(10)
                                    with context.wrap_socket(connection, server_side=True) as bootstrap:
                                        request = bytearray()
                                        while b'\r\n\r\n' not in request:
                                            chunk = bootstrap.recv(1024)
                                            if not chunk:
                                                raise AssertionError('bootstrap request truncated')
                                            request.extend(chunk)
                                        if bytes(request) != b'GET /config/fixture HTTP/1.1\r\nHost: original.invalid\r\n\r\n':
                                            raise AssertionError('bootstrap HTTP bytes changed')
                                        bootstrap.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: 9\r\n\r\nbootstrap')
                    except Exception as error:
                        errors.append(repr(error))
                worker = threading.Thread(target=serve, daemon=True)
                worker.start()
                arguments = [str(exe), str(library), str(root / 'root.der') if trust else '-',
                    str(listener.getsockname()[1]), 'trusted' if accepted else 'untrusted', address]
                if accepted:
                    arguments.extend(['truncated' if abrupt else 'complete', str(lobby_listener.getsockname()[1]), str(game.getsockname()[1])])
                result = subprocess.run(arguments,
                    capture_output=True, timeout=20)
                worker.join(18)
                label = f'address={address} trusted-root={trust} expired={expired} accepted={accepted} abrupt={abrupt}'
                if result.returncode or worker.is_alive() or errors:
                    print(label, result.stdout.decode(errors='replace'), result.stderr.decode(errors='replace'), errors)
                    return 1
                if address.endswith('.test') and names != [address] * (7 if accepted else 1):
                    raise AssertionError(f'SNI mismatch: {names} for {address}')
                print('PASS', label)
    return 0


if __name__ == '__main__':
    sys.exit(main())
