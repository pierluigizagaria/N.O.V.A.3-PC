"""Independent processes must preserve preferences and synthetic identities."""
from pathlib import Path
import hashlib
import re
import subprocess
import sys
import tempfile

exe, fixture, root = sys.argv[1:]
Path(root).mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='profile-', dir=root) as temp:
    def run(mode, profile):
        r = subprocess.run([exe, fixture, mode, str(Path(temp) / profile)],
                           text=True, capture_output=True, timeout=15)
        assert r.returncode == 0, (r.stdout, r.stderr)
        assert "case-sensitive guest paths alias on Windows" in r.stderr
        ids = dict(re.findall(r'^ID (\w+)=(.+)$', r.stdout, re.M))
        assert len(ids) == 6, r.stdout
        mac = ids['mac']
        first = int(mac[:2], 16)
        assert first & 2 and not first & 1
        expected = hashlib.md5(mac.encode()).hexdigest().lstrip('0') or '0'
        assert 'MD5=' + expected in r.stdout
        assert re.fullmatch(r'[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}', ids['udid'])
        assert ids['imei'].isdigit() and len(ids['imei']) == 15
        return ids
    a = run('profile-write', 'a')
    assert run('profile-read', 'a') == a
    b = run('profile-write', 'b')
    assert run('profile-read', 'b') == b
    assert all(a[key] != b[key] for key in a)
    assert run('profile-read', 'a') == a
    print('PASS five processes, two independent profiles, six stable random IDs per profile')
