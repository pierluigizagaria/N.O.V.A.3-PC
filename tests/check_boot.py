"""Opt-in pre-GL completion, three fresh processes using one persistent profile."""
from pathlib import Path
import re
import subprocess
import sys
import time
from isolated_runtime import isolated_executable

executable, library, profile = sys.argv[1:]
if not Path(library).is_file():
    print('SKIP: local/input x86 library is absent')
    sys.exit(77)
with isolated_executable(executable, profile) as executable:
    for attempt in range(3):
        start = time.perf_counter()
        r = subprocess.run([executable, library, '--profile-root', profile],
                           capture_output=True, text=True, timeout=45)
        assert r.returncode == 0, (r.returncode, r.stdout[-4000:], r.stderr)
        assert 'initializers completed: 184 (array slots: 184)' in r.stdout
        assert 'JNI_OnLoad returned=0x00010004' in r.stdout
        assert 'imports=331 implemented=227 placeholders=3 trapped=101' in r.stdout
        assert 'trapped_groups GL=100 OpenSL=1 sockets=0 other=0' in r.stdout
        assert 'installer nativeAllow=1' in r.stdout
        assert not re.search(r'^native .*_(initGL|InitViewSettings|resize|step)$', r.stdout, re.M)
        assert 'pre-GL startup complete; createView_requested=1; next native=initGL' in r.stdout
        assert 'boot_ms=' in r.stdout
        print(f'PRE-GL COMPLETE run={attempt + 1} DRM nativeAllow=1 ' +
              re.search(r'boot_ms=.*', r.stdout).group(0))
        print(f'probe process wall time: {(time.perf_counter()-start)*1000:.1f} ms')
