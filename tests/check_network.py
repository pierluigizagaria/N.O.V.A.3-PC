"""Two ELF32 guests exchange TCP payloads through separate local profiles."""
from pathlib import Path
import queue
import re
import subprocess
import sys
import tempfile
import threading

exe, fixture, root = sys.argv[1:]
Path(root).mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='network-', dir=root) as temp:
    a, b = Path(temp) / 'a', Path(temp) / 'b'
    server = subprocess.Popen([exe, fixture, 'server', str(a)], stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, text=True)
    try:
        lines = queue.Queue()
        def reader():
            for line in server.stdout:
                lines.put(line)
        thread = threading.Thread(target=reader, daemon=True)
        thread.start()
        ready = lines.get(timeout=10)
        assert ready.startswith('READY '), ready
        port = ready.split()[1]
        client = subprocess.run([exe, fixture, 'client', str(b), port],
                                text=True, capture_output=True, timeout=10)
        assert client.returncode == 0, (client.stdout, client.stderr)
        server.wait(timeout=10)
        thread.join(timeout=2)
        assert server.returncode == 0, server.stderr.read()
        total = 0
        for profile, role in [(a, 'server'), (b, 'client')]:
            logs = list((profile / 'logs').glob('socket-*.log'))
            assert logs
            text = ''.join(p.read_text() for p in logs)
            assert 'send length=' in text and 'recv length=' in text
            assert ' hex=' in text and 'local=127.0.0.1:' in text and 'peer=127.0.0.1:' in text
            assert '7365636f6e642d70726f66696c6521' in text
            assert '66697273742d70726f66696c6521' in text
            for endpoint in re.findall(r'(?:local|peer)=([^ ]+)', text):
                assert endpoint.startswith('127.0.0.1:'), endpoint
            total += sum(map(int, re.findall(r'length=(\d+)', text)))
            assert (profile / 'preferences.db').is_file()
        assert total == 58, total
        print('PASS two profiles, 29 payload bytes exchanged, 58 logged send/receive bytes; localhost only')
    finally:
        if server.poll() is None:
            server.kill()
            server.wait(timeout=5)
