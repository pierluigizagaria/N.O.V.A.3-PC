"""Loopback-only integration for the real configured WinHTTP price callback."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import os
import datetime
import ssl
import subprocess
import sys
import tempfile
import threading
import unittest

EXE = Path(sys.argv.pop(1))
WORK = Path(sys.argv.pop(1))
DEBUG = sys.argv.pop(1) == 'Debug'
WORK.mkdir(parents=True, exist_ok=True)


class Handler(BaseHTTPRequestHandler):
    status = 200
    rows = 54
    requests = []

    def do_GET(self):
        type(self).requests.append((self.path, self.headers.get('Authorization'), self.headers.get('Host')))
        self.send_response(type(self).status)
        self.send_header('Location', 'http://unreachable.invalid/never-follow')
        self.end_headers()
        self.wfile.write(b'<offline_item_prices><items>' +
                        b'<item><price_value>0</price_value><previous_price_value>0</previous_price_value></item>' * type(self).rows +
                        b'</items></offline_item_prices>')

    def log_message(self, *_):
        pass


class PriceTransport(unittest.TestCase):
    def test_environment_bridge_is_debug_only(self):
        with ThreadingHTTPServer(('127.0.0.1', 0), Handler) as server, tempfile.TemporaryDirectory(dir=WORK) as directory:
            worker = threading.Thread(target=server.serve_forever, daemon=True)
            worker.start()
            try:
                config = Path(directory) / 'legacy.ini'
                url = 'http://legacy.invalid/exact?product=1'
                config.write_text(f'origin=http://127.0.0.1:{server.server_port}\nrequest_url={url}\n'
                                  'version=unit-v1\ntransport=native\nprice_rows=54\n')
                Handler.requests.clear()
                Handler.status, Handler.rows = 200, 54
                result = subprocess.run([str(EXE), '--environment-probe', url], capture_output=True, timeout=10,
                    env={**os.environ, 'NOVA_TEST_SHOP_CONFIG': str(config)})
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(len(Handler.requests), 1 if DEBUG else 0)
            finally:
                server.shutdown()
                worker.join()

    def test_explicit_address_identity_and_offline_override(self):
        # A distinct loopback address proves the bridge no longer hardcodes .1.
        with ThreadingHTTPServer(('127.1.1.2', 0), Handler) as server, tempfile.TemporaryDirectory(dir=WORK) as directory:
            from cryptography import x509
            from cryptography.hazmat.primitives import hashes, serialization
            from cryptography.hazmat.primitives.asymmetric import rsa
            from cryptography.x509.oid import NameOID
            root = Path(directory)
            key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
            name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'synthetic price service')])
            now = datetime.datetime.now(datetime.timezone.utc)
            cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key())
                    .serial_number(x509.random_serial_number()).not_valid_before(now - datetime.timedelta(days=1))
                    .not_valid_after(now + datetime.timedelta(days=1)).sign(key, hashes.SHA256()))
            (root / 'cert.pem').write_bytes(cert.public_bytes(serialization.Encoding.PEM))
            (root / 'key.pem').write_bytes(key.private_bytes(serialization.Encoding.PEM,
                serialization.PrivateFormat.TraditionalOpenSSL, serialization.NoEncryption()))
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.minimum_version = ssl.TLSVersion.TLSv1_2
            context.load_cert_chain(root / 'cert.pem', root / 'key.pem')
            server.socket = context.wrap_socket(server.socket, server_side=True)
            worker = threading.Thread(target=server.serve_forever, daemon=True)
            worker.start()
            try:
                root = Path(directory)
                config = root / 'server.conf'
                text = 'mode=online\naddress=127.1.1.2\n'
                authority = f'confirmation.gameloft.com:{server.server_port}'
                original_url = f'http://{authority}/exact?product=1'
                config.write_text(text)
                Handler.requests.clear()
                for status, rows, expected in [(200, 54, 0), (302, 54, 1), (200, 53, 1), (404, 54, 1)]:
                    Handler.status, Handler.rows = status, rows
                    result = subprocess.run([str(EXE), '--server-fetch', str(config), original_url, str(server.server_port)], capture_output=True, timeout=10)
                    self.assertEqual(result.returncode, expected, result.stderr)
                self.assertEqual(Handler.requests, [('/exact?product=1', None, authority)] * 4)
                # Explicit offline suppresses even an inherited legacy bridge.
                legacy = root / 'legacy.ini'
                legacy.write_text(f'origin=http://127.0.0.1:{server.server_port}\nrequest_url=http://legacy.invalid/exact?product=1\n'
                                  'version=unit-v1\ntransport=native\nprice_rows=1\n')
                config.write_text('mode=offline\n')
                result = subprocess.run([str(EXE), '--server-offline', str(config), original_url], capture_output=True, timeout=10,
                                        env={**os.environ, 'NOVA_TEST_SHOP_CONFIG': str(legacy)})
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(len(Handler.requests), 4)
            finally:
                server.shutdown()
                worker.join()


if __name__ == '__main__':
    unittest.main()
