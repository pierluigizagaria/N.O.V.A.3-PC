"""Read-only retention guard. Never deletes or reads player/game contents."""
from pathlib import Path
import argparse
import re


def violations(root: Path) -> list[str]:
    errors = []
    # Build/test inputs must not depend on disposable directories. Runtime log
    # destinations in C++/Python are allowed; CMake and the server tool are not.
    sources = [root / 'CMakeLists.txt', root / 'tests/CMakeLists.txt',
               root / 'server/dev.ps1', *sorted((root / 'tests').glob('*.cmake')),
               *sorted((root / 'cmake').glob('*.cmake'))]
    for path in sources:
        if path.exists() and re.search(r'local[/\\](?:logs|work)(?:[/\\]|["\'])',
                                     path.read_text(encoding='utf-8')):
            errors.append(f'{path.relative_to(root)}: dependency on disposable local folder')
    for name in ('work', 'codex', 'audio/tests', 'candidates', 'test-profiles'):
        path = root / 'local' / name
        if path.exists() and any(p.is_file() for p in path.rglob('*')):
            errors.append(f'local/{name}: generated residue; review before finishing the task')
    for path in (root / 'local/analysis').rglob('*'):
        if path.is_file() and path.suffix.lower() in {'.exe', '.dll', '.pdb', '.obj', '.log'}:
            errors.append(f'{path.relative_to(root)}: generated diagnostic/build output needs cleanup')
    logs = root / 'local/logs'
    if logs.exists():
        files = [p for p in logs.rglob('*') if p.is_file()]
        forbidden = {'.cpp', '.h', '.inc', '.cmake', '.so', '.dll', '.exe', '.pdb',
                     '.bsprite', '.bin', '.db', '.conf', '.sav', '.zip'}
        for path in files:
            if path.suffix.lower() in forbidden:
                errors.append(f'{path.relative_to(root)}: source, state or input belongs elsewhere')
        if len(files) > 20 or sum(p.stat().st_size for p in files) > 32 * 1024 * 1024:
            errors.append('local/logs: exceeds 20 files / 32 MiB; summarize or investigate')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    errors = violations(args.root.resolve())
    for error in errors:
        print(error)
    print(f'Local retention: {len(errors)} issue(s). No files changed.')
    return bool(errors)


if __name__ == '__main__':
    raise SystemExit(main())
