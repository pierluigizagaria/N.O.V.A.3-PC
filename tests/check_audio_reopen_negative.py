"""Accept only the named production-accounting failure, not an arbitrary crash."""
import subprocess
import sys

result = subprocess.run(sys.argv[1:], capture_output=True, text=True, timeout=15)
sys.stdout.write(result.stdout)
sys.stderr.write(result.stderr)
expected = "REOPEN_TAINTED_QUEUE: consumed/tapped PCM admitted to empty stream"
if result.returncode != 1 or expected not in result.stderr:
    raise SystemExit("negative fixture did not fail at the required transition invariant")
print("discriminating missing-boundary control observed")
