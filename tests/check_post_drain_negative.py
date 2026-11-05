"""Later runner only: require the exact controlled failure, never any crash."""
import subprocess
import sys
mode, executable, fixture, *extra = sys.argv[1:]
expected = {
    "--queue-host-packets": "STALE_COPIED_PACKET_REAUTHORIZED_AFTER_REVOCATION",
    "--retain-old-caller": "OBSOLETE_NATIVE_CALLER_CONTINUATION_AFTER_DELIVERY",
    "--omit-role-revocation": "NORMAL_PAD_AUTHORITY_LEAKED_TO_LATER_ROLE_TAKE",
}[mode]
result = subprocess.run([executable, fixture, mode, *extra], capture_output=True, text=True)
if result.returncode == 77:
    sys.exit(77)
text = result.stdout + result.stderr
if result.returncode != 1 or expected not in text:
    print(text)
    raise SystemExit("missing exact post-drain negative witness: " + expected)
print(expected + ": exact rejection observed")
