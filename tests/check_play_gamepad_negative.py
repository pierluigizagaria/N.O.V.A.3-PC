"""Require the intended consumer-level omission, never an arbitrary crash."""
import subprocess
import sys
mode, executable, fixture, *extra = sys.argv[1:]
expected = {
    "--omit-play-poll": "PLAY_ROUTE_CONSUMER_AXES_MISSING",
    "--omit-play-gate": "PLAY_ROUTE_GATE_LEFT_NATIVE_ANALOG",
}[mode]
result = subprocess.run([executable, fixture, mode, *extra], capture_output=True, text=True)
if result.returncode == 77:
    sys.exit(77)
output = result.stdout + result.stderr
if result.returncode != 1 or expected not in output:
    print(output)
    raise SystemExit("missing exact play routing omission: " + expected)
print(expected + ": exact rejection observed")
