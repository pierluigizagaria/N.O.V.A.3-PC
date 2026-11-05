"""Integrator-only runner: exact negative failures, never count crashes as passes."""
import argparse
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--exe", required=True)
parser.add_argument("--fixture", required=True)
parser.add_argument("--case", required=True, choices=("baseline", "omit-dispatch", "host-layout", "oper-up"))
args = parser.parse_args()
try:
    result = subprocess.run([args.exe, args.fixture, args.case], capture_output=True, text=True, timeout=15)
except (OSError, subprocess.TimeoutExpired) as error:
    print(f"FAIL runner {error}", file=sys.stderr)
    sys.exit(1)
output = result.stdout + result.stderr
print(output, end="")
if args.case == "baseline":
    ok = result.returncode == 0 and "PASS interface-ioctl imports=production provider=synthetic sdk-adapter=production" in output
    ok = ok and "FAIL" not in output and "INJECT" not in output
else:
    tag = {"omit-dispatch": "count-only", "host-layout": "count-length", "oper-up": "flags-value"}[args.case]
    ok = result.returncode == 1 and f"INJECT {args.case}\n" in output and f"FAIL GUEST {tag} line=" in output
    ok = ok and "PASS interface-ioctl" not in output and "FAIL HOST" not in output
if not ok:
    print(f"FAIL runner case={args.case} exit={result.returncode}", file=sys.stderr)
sys.exit(0 if ok else 1)
