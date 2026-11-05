"""The actual lifecycle OOM branch must terminate before native forwarding."""
import subprocess
import sys

result = subprocess.run([sys.argv[1], sys.argv[2], '--fail-event'],
                        capture_output=True, text=True, timeout=20)
output = result.stdout + result.stderr
assert result.returncode == 74, (result.returncode, output)
assert 'event-failure-child-started' in output, output
assert 'world_fov: fatal lifecycle event allocation failure' in output, output
assert 'INVALID-CONTINUED-AFTER-EVENT-FAILURE' not in output, output
assert 'INVALID-AFTER-OOM-MUTATION-OR-QUERY' not in output, output
assert 'world FOV installed adapter' not in output, output
print('Lifecycle allocation failure exits 74 before native original/queries/continuation')
