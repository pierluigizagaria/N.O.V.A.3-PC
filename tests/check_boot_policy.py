"""Opt-in production startup regression using only disposable host preferences."""
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from isolated_runtime import isolated_executable

executable, library, directory = sys.argv[1:]
if not Path(library).is_file():
    print('SKIP: local/input x86 library is absent')
    sys.exit(77)
Path(directory).mkdir(parents=True, exist_ok=True)


def encode(value):
    return value.encode().hex() or '-'


def read(path):
    values = {}
    for row in path.read_text().splitlines()[1:]:
        space, key, value = (bytes.fromhex(x).decode() if x != '-' else '' for x in row.split())
        values[space, key] = value
    return values


with isolated_executable(executable, directory) as executable, \
        tempfile.TemporaryDirectory(prefix='policy-', dir=directory) as temp:
    now = int(time.time())
    cases = [('expired', 'LICENSED', str(now - 1), True),
             ('zero', 'LICENSED', '0', True),
             ('future', 'LICENSED', str(now + 172800), True),
             ('refused-future', 'NOT_LICENSED', str(now + 172800), True),
             ('retry', 'RETRY', '0', True),
             ('unknown', 'UNKNOWN', '0', False),
             ('malformed', 'LICENSED', '0junk', False),
             ('native-veto', 'LICENSED', str(now + 172800), False)]
    for name, response, expiry, allowed in cases:
        profile = Path(temp) / name
        profile.mkdir()
        policy = {'lastResponse': response, 'validityTimestamp': expiry, 'retryUntil': '0',
                  'maxRetries': '0', 'retryCount': '0', 'RunFirst': 'false',
                  'gdrm_r_time': str(now), 'gdrm_l_time': str(now)}
        if name == 'native-veto':
            # A future effective native clock defeats a future wall-clock seed.
            policy['gdrm_r_time'] = policy['gdrm_l_time'] = str(now + 259200)
        if name == 'retry':
            policy['retryUntil'] = str(now + 3600)
            policy['maxRetries'] = '2'
        before = {('GDRMPolicy', k): v for k, v in policy.items()}
        before['MySavedData', 'player-sentinel'] = 'keep-save-control-config'
        db = profile / 'preferences.db'
        db.write_text('NOVA3PC_PROFILE_V1\n' + ''.join(
            f'{encode(space)} {encode(key)} {encode(value)}\n'
            for (space, key), value in sorted(before.items())), newline='')
        started = int(time.time())
        result = subprocess.run([executable, library, '--profile-root', str(profile)],
                                capture_output=True, text=True, timeout=15)
        assert result.returncode == (0 if allowed else 1), (name, result.stdout[-2500:], result.stderr)
        assert '_initGL' not in result.stdout and '_InitViewSettings' not in result.stdout
        after = read(db)
        assert after['MySavedData', 'player-sentinel'] == before['MySavedData', 'player-sentinel']
        for key, value in before.items():
            if name in ['native-veto', 'unknown'] and key[0] == 'GDRMPolicy':
                continue  # The unchanged native denial/retry callbacks own these values.
            if name == 'retry' and key == ('GDRMPolicy', 'retryCount'):
                assert after[key] == '1'
                continue
            if allowed and key in [('GDRMPolicy', 'validityTimestamp'),
                                    ('GDRMPolicy', 'gdrm_r_time'), ('GDRMPolicy', 'gdrm_l_time')]:
                continue  # Native time callbacks are exercised, rather than suppressed.
            assert after[key] == value, (name, key, value, after[key])
        if allowed:
            assert 'installer nativeAllow=1' in result.stdout
            assert 'pre-GL startup complete; createView_requested=1' in result.stdout
            actual_expiry = int(after['GDRMPolicy', 'validityTimestamp'])
            if name in ['future', 'refused-future', 'retry']:
                assert actual_expiry == int(expiry)
            else:
                assert started + 86400 <= actual_expiry <= int(time.time()) + 86400
        else:
            assert 'pre-GL startup complete' not in result.stdout
            assert '_GameInstaller_nativeStart' not in result.stdout
            if name in ['native-veto', 'unknown']:
                assert 'installer nativeAllow=0' in result.stdout
        print(f'PASS host policy {name}: allowed={allowed}, player sentinel preserved, no GL')
