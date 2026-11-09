"""Check the real client CLI without starting a game or loading guest code."""
from pathlib import Path
import subprocess
import sys

exe = Path(sys.argv[1]).resolve()
debug = sys.argv[2] == 'Debug'
result = subprocess.run([str(exe), '--help'], capture_output=True, timeout=10)
assert result.returncode == 0, result.stderr
assert b'--server-config' in result.stdout
options = ['--frames', '--screenshot-after', '--input-script', '--audio-dump',
           '--allow-physical-input', '--input-diagnostics', '--initial-menu-observer',
           '--diagnostic-menu-ack', '--session-log', '--net-log', '--file-log', '--local-ca']
for option in options:
    assert (option.encode() in result.stdout) == debug, option
    if not debug:
        rejected = subprocess.run([str(exe), 'missing-library.so', option, '1'], capture_output=True, timeout=10)
        assert rejected.returncode != 0 and b'Unsupported option' in rejected.stderr, (option, rejected.stderr)
print('PASS product CLI', sys.argv[2])
