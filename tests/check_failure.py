import subprocess
import sys

executable, fixtures, mode = sys.argv[1:]
result = subprocess.run([executable, fixtures, mode], capture_output=True, text=True, timeout=20)
expected = {
    'trap-first': (70, 'unimplemented import: missing_first', 'trigger_first+'),
    'trap-second': (70, 'unimplemented import: missing_second', 'trigger_second+'),
    'jni_trap': (70, 'unimplemented import: JNI.DefineClass', 'fixture_jni_trap+'),
    'crash': (71, 'guest exception 0xc0000005', 'crash_site+'),
}
code, message, symbol = expected[mode]
assert result.returncode == code, (result.returncode, result.stdout, result.stderr)
assert message in result.stderr, result.stderr
assert symbol in result.stderr, result.stderr
assert 'stack scan (candidate return addresses' in result.stderr, result.stderr
assert 'esp+0x' in result.stderr, result.stderr
print(f'{mode}: exit={code}, diagnostic and symbolized stack verified')
