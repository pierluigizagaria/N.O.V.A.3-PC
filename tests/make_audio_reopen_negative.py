"""Future integrator-only mutation: remove boundary guards in a generated build copy."""
import pathlib
import sys

source, output = map(pathlib.Path, sys.argv[1:])
if source.resolve() == output.resolve():
    raise SystemExit("refusing to overwrite production source")
text = source.read_text(encoding="utf-8")
guard = " || !pristine_queue(o)"
if text.count(guard) != 2:
    raise SystemExit("unexpected boundary guard count; review source drift")
output.parent.mkdir(parents=True, exist_ok=True)
text = text.replace(guard, "")
# Keep the internal helper referenced under /W4 /WX without enforcing its result.
text = text.replace("    o.reopen_pending = false; // One attempt",
                    "    (void)pristine_queue(o); // Mutated fixture only.\n"
                    "    o.reopen_pending = false; // One attempt", 1)
output.write_text(text, encoding="utf-8")
