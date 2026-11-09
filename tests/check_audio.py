"""Process-level preflight and named OpenSL trap checks; no game inputs."""
from pathlib import Path
import subprocess
import sys
import tempfile
from isolated_runtime import isolated_executable

mode, executable, fixture, root = sys.argv[1:]
root = Path(root)
root.mkdir(parents=True, exist_ok=True)
with isolated_executable(executable, root) as executable, \
        tempfile.TemporaryDirectory(prefix="audio-", dir=root) as work:
    if mode == "trap":
        result = subprocess.run([executable, fixture, "trap", work], capture_output=True, text=True, timeout=10)
        assert result.returncode == 70, result.stdout + result.stderr
        assert "unimplemented OpenSL method: EnableStereoPosition" in result.stderr, result.stderr
    else:
        result = subprocess.run([executable, "deliberately-missing-library.so", "--profile-root", work],
                                capture_output=True, text=True, timeout=10)
        assert result.returncode == 1, result.stdout + result.stderr
        assert "Profile is not prepared" in result.stderr, result.stderr
        assert "setup_inputs.py --prepare-profile" in result.stderr, result.stderr
        assert "40 missing/empty bootstrap files" in result.stderr, result.stderr
        assert "init_array" not in result.stdout, result.stdout
        assert "cannot open" not in result.stderr.lower(), result.stderr
        data = Path(work) / "sdcard/Android/data/com.gameloft.android.ANMP.GloftN3HM/files"
        inventory = Path(__file__).resolve().parents[1] / "assets/manifests/bootstrap_files.txt"
        for name in inventory.read_text().splitlines():
            (data / name).write_bytes(b"synthetic")
        for args, message in [(["--audio", "bad"], "Audio backend must be sdl or null"),
                              (["--audio", "null", "--audio-dump", "outside.wav"], "under local/")]:
            result = subprocess.run([executable, "deliberately-missing-library.so", "--profile-root", work, *args],
                                    capture_output=True, text=True, timeout=10)
            assert result.returncode == 1 and message in result.stderr, result.stdout + result.stderr
print("PASS audio-" + mode)
