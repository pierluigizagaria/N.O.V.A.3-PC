"""Opt-in test; reads only the supplied library and never invokes JNI_OnLoad."""
from pathlib import Path
import re
import subprocess
import sys
import time

executable, library = sys.argv[1:]
if not Path(library).is_file():
    print('SKIP: local/input x86 library is absent')
    sys.exit(77)
start = time.perf_counter()
result = subprocess.run([executable, library, '--run-init'], capture_output=True, text=True, timeout=45)
elapsed = time.perf_counter() - start
assert result.returncode == 0, (result.returncode, result.stdout[-6000:], result.stderr)
for name, count in [('RELATIVE', 57930), ('JMP_SLOT', 325), ('GLOB_DAT', 11), ('32', 5)]:
    assert f'R_386_{name}={count}\n' in result.stdout, name
assert 'imports=331 ' in result.stdout
assert len(re.findall(r'^import ', result.stdout, re.M)) == 331
assert len(re.findall(r'^export Java_', result.stdout, re.M)) == 55
assert re.search(r'^export JNI_OnLoad 0x[0-9a-f]+$', result.stdout, re.M)
assert re.search(r'^export JNI_GetEnv 0x[0-9a-f]+$', result.stdout, re.M)
assert 'pointer_bits=32' in result.stdout
assert 'initializers completed: 184 (array slots: 184)' in result.stdout
assert '__cxa_atexit registrations=287' in result.stdout
assert not result.stderr, result.stderr
for line in result.stdout.splitlines():
    if line.startswith(('base=', 'R_386_', 'imports=', 'Java_exports=', 'initializers completed:',
                        '__cxa_atexit')):
        print(line)
print(f'load + bind + constructors + process startup: {elapsed * 1000:.1f} ms')
