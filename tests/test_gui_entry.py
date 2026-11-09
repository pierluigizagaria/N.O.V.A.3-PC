"""Verify the shipped PE subsystem and shared Windows entry with real log pipes."""
import pathlib
import struct
import subprocess
import sys

for name in sys.argv[1:]:
    data = pathlib.Path(name).read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[pe:pe+4] == b'PE\0\0'
    assert struct.unpack_from('<H', data, pe+4)[0] == 0x14c, 'x86 required'
    assert struct.unpack_from('<H', data, pe+24+68)[0] == 2, 'GUI subsystem required'
run = subprocess.run([sys.argv[1], 'argument with spaces'], capture_output=True,
                     text=True, timeout=10)  # Intentionally no CREATE_NO_WINDOW.
assert run.returncode == 7, run
assert run.stdout == 'GUI stdout preserved\n', run
assert run.stderr == 'GUI stderr preserved\n', run
print('PASS native GUI process, argument quoting, exit status and both log pipes')
