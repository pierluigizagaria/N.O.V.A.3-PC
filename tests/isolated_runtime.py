"""Temporary host runtime for tests that must not read adjacent user settings."""
from contextlib import contextmanager
from pathlib import Path
import shutil
import tempfile


@contextmanager
def isolated_executable(executable, parent):
    source = Path(executable).resolve(strict=True)
    parent = Path(parent)
    parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='host-runtime-', dir=parent) as work:
        target = Path(work) / source.name
        shutil.copy2(source, target)
        for library in source.parent.glob('*.dll'):
            shutil.copy2(library, target.parent / library.name)
        yield str(target)
