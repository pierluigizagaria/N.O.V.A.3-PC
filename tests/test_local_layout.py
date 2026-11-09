"""Regression coverage for forbidden dependencies and non-destructive auditing."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('layout', Path(__file__).resolve().parents[1] / 'tools/check_local_layout.py')
layout = importlib.util.module_from_spec(spec)
spec.loader.exec_module(layout)


class LayoutTests(unittest.TestCase):
    def test_detects_inputs_and_residue_without_changing_state(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            (root / 'tests').mkdir()
            dependency = root / 'tests/fixture.cmake'
            dependency.write_text('include("${PROJECT_SOURCE_DIR}/local/work/probe.cmake")')
            state = root / 'local/logs/settings.conf'
            state.parent.mkdir(parents=True)
            state.write_bytes(b'preserved state')
            self.assertEqual(len(layout.violations(root)), 2)
            self.assertEqual(state.read_bytes(), b'preserved state')
            dependency.write_text('include("fixture.cmake")')
            (root / 'local/settings.conf').write_bytes(state.read_bytes())
            state.unlink()
            self.assertEqual(layout.violations(root), [])


if __name__ == '__main__':
    unittest.main()
