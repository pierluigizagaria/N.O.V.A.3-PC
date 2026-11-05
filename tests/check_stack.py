"""Original ELF regression across real host entry domains; no game or user input."""
import hashlib
import json
import re
from pathlib import Path
import struct
import subprocess
import sys
import time

runner, fixture, output = map(Path, sys.argv[1:])
output.mkdir(parents=True, exist_ok=True)
run = str(time.time_ns())


def pe_stack(path):
    data = path.read_bytes()
    at = struct.unpack_from('<I', data, 60)[0]
    assert data[:2] == b'MZ' and data[at:at+4] == b'PE\0\0'
    assert struct.unpack_from('<H', data, at+4)[0] == 0x14c
    assert struct.unpack_from('<H', data, at+24)[0] == 0x10b
    return struct.unpack_from('<II', data, at+24+72)


assert pe_stack(runner) == (1048576, 1048576), 'guest consumer must inherit committed PE policy'
# Check the emitted original machine-code sequence, without optional disassemblers.
elf = fixture.read_bytes()
assert elf[:5] == b'\x7fELF\x01' and struct.unpack_from('<H', elf, 18)[0] == 3
assert b'\x2b\x26\xe8\x0e\x00\x00\x00' in elf, 'unprobed SUB ESP,[ESI] immediately followed by local CALL'
assert b'\x0f\x29\x44\x24\x04' in elf, 'aligned SSE at the new frame'
assert b'chkstk' not in elf and b'alloca' not in elf, 'no Windows compiler stack probe'

cases = [
    ('main', 0x20008, 0), ('main', 0x28f8, 0),
    ('thread', 0x20008, 0), ('thread', 0x28f8, 0),
    ('callback', 0x20008, 0), ('callback', 0x28f8, 0),
    ('pthread', 0x20008, 0), ('pthread', 0x20008, 1048576),
    ('pthread', 0x9008, 65536), ('pthread', 0x9008, 65537),
    ('pthread', 0x120008, 2097152), ('pthread', 24, 8192),
    ('detach', 0x9008, 65536), ('guard0', 24, 65536),
    ('guard8192', 24, 65536), ('failures', 24, 0),
]
records = []
for index, (mode, frame, size) in enumerate(cases):
    result = subprocess.run([str(runner), str(fixture), mode, str(frame), str(size)],
                            capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW,
                            timeout=15)
    text = result.stdout.decode('utf-8', errors='replace')
    prefix = output / f'{run}-{index:02}-{mode}'
    prefix.with_suffix('.stdout.log').write_bytes(result.stdout)
    prefix.with_suffix('.stderr.log').write_bytes(result.stderr)
    assert result.returncode == 0 and f'PASS stack {mode}' in text, (mode, frame, size, result.returncode, text, result.stderr)
    if mode != 'failures':
        assert 'state=0x1000 protect=0x4' in text and 'alignment=16' in text
        line = next(line for line in text.splitlines() if line.startswith('stack before='))
        memory = {key: int(value, 0) for key, value in re.findall(r'(\w+)=(0x[\da-f]+|\d+)', line)}
        assert memory['committed'] >= (size or 1048576)
        if memory['guard_pages'] == 0:
            assert memory['committed'] == memory['base'] - memory['allocation']
    else:
        memory = None
    records.append({'mode': mode, 'frame': frame, 'requested': size,
                    'exit': result.returncode,
                    'memory': memory,
                    'stdout_sha256': hashlib.sha256(result.stdout).hexdigest(),
                    'stderr_sha256': hashlib.sha256(result.stderr).hexdigest()})
receipt = {'runner_sha256': hashlib.sha256(runner.read_bytes()).hexdigest(),
           'fixture_sha256': hashlib.sha256(elf).hexdigest(), 'cases': records}
(output / f'{run}-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
print(f'{len(records)} synthetic stack cases passed: main/std::thread/OpenSL/pthread, commit, unprobed CALL/SSE, attributes, cleanup and real failure')
