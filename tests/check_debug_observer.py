"""Exercise only owned PE32 fixtures. Never load the Android library or a profile."""
import hashlib
import json
import pathlib
import shutil
import subprocess
import struct
import sys
import time
import tempfile

observer, child, module, root = map(pathlib.Path, sys.argv[1:])
with tempfile.TemporaryDirectory(prefix='nova-observer-') as temporary_root:
    root = pathlib.Path(temporary_root)
    out = root / 'local' / 'test-profiles' / ('b02-observer-' + observer.parent.name.lower())
    out.mkdir(parents=True, exist_ok=True)
    run_id = str(time.time_ns())
    hidden = subprocess.CREATE_NO_WINDOW

    def run(name, mode='normal', failure=None, args=(), expected=37, input_data=None):
        prefix = out / (run_id + '-' + name)
        witness = pathlib.Path(str(prefix) + '.witness')
        command = [str(observer), '--output', str(prefix), '--fixture', str(child)]
        if failure:
            command += ['--fixture-failure', failure]
        command += ['--', mode, str(witness), *map(str, args)]
        result = subprocess.run(command, cwd=root, capture_output=True, timeout=30, input=input_data,
                                creationflags=hidden)
        assert pathlib.Path(str(prefix) + '.summary.json').exists(), (name, result.returncode, result.stdout, result.stderr)
        summary = json.loads(pathlib.Path(str(prefix) + '.summary.json').read_text(encoding='utf-8'))
        events = [json.loads(line) for line in pathlib.Path(str(prefix) + '.events.jsonl').read_text(encoding='utf-8').splitlines()]
        stdout = pathlib.Path(str(prefix) + '.stdout.log').read_bytes()
        stderr = pathlib.Path(str(prefix) + '.stderr.log').read_bytes()
        if failure == 'alias':
            assert not summary['wait_completed'] and summary['exit_code_unsigned'] is None
            assert summary['stdout_sha256'] is None and summary['stderr_sha256'] is None
            for _ in range(100):
                if witness.exists():
                    break
                time.sleep(.02)
            stdout = pathlib.Path(str(prefix) + '.stdout.log').read_bytes()
            stderr = pathlib.Path(str(prefix) + '.stderr.log').read_bytes()
        else:
            assert summary['wait_completed'] and summary['exit_code_unsigned'] == expected, (name, summary, result.stderr)
        assert result.returncode == (1 if failure else 0), (name, result.returncode, result.stderr, summary)
        assert summary['incomplete'] == bool(failure), (name, summary)
        if failure != 'alias':
            assert summary['stdout_sha256'] == hashlib.sha256(stdout).hexdigest()
            assert summary['stderr_sha256'] == hashlib.sha256(stderr).hexdigest()
        assert summary['event_bytes'] <= 16 * 1024 * 1024
        assert [e['seq'] for e in events] == sorted(e['seq'] for e in events)
        if expected == 37:
            assert witness.read_bytes() == b'fixture_completed'
            assert b'fixture_completed' in stdout and b'fixture_stderr' in stderr
        else:
            assert not witness.exists(), (name, 'unexpected normal completion')
        if failure in ['safety', 'continue', 'throw', 'metadata', 'resident', 'ntdll-throw']:
            assert b'observer resident attention:' in result.stderr, (name, result.stderr)
        return summary, events, stdout

    def exceptions(events):
        result = [e for e in events if e['kind'] == 'exception' and not e['os_startup']]
        for event in result:
            assert 'linked_record_address' in event
            assert event['linked_record_address'] is None or isinstance(event['linked_record_address'], str)
            assert isinstance(event['parameters_truncated'], bool) and not event['parameters_truncated']
            if event['code'] == '0xc0000005':
                assert event['parameter_count'] == len(event['parameters']) == 2
        return result

    s, e, stdout = run('normal')
    assert s['startup_consumed'] and s['exit_event_seen'] and not s['detached']
    assert len([x for x in e if x['kind'] == 'exception' and x['os_startup']]) == 1
    assert any(x['kind'] == 'debug_string_skipped' and x['skipped_bytes'] > 0 for x in e)
    s, e, _ = run('handled', 'handled')
    av = exceptions(e)
    assert len(av) == 1 and av[0]['code'] == '0xc0000005' and av[0]['first_chance'] == 1
    s, e, _ = run('unhandled', 'av', expected=0xc0000005)
    av = exceptions(e)
    assert [x['first_chance'] for x in av] == [1, 0]
    assert av[0]['address'] == av[1]['address'] and av[0]['context']['edx'] == '0x00000001'
    s, e, stdout = run('worker', 'worker', expected=0xc0000005)
    av = exceptions(e)
    assert av[0]['tid'] != s['primary_tid']
    assert f"worker_tid={av[0]['tid']}".encode() in stdout
    for reg, value in {'eax':'0x11223344','ecx':'0x55667788','ebx':'0x13579bdf','esi':'0x2468ace0','edi':'0x10203040'}.items():
        assert av[0]['context'][reg] == value, (reg, av[0]['context'])
    assert av[0]['ip_memory']['read'] > 0 and av[0]['stack_memory']['read'] > 0
    assert av[0]['target_memory']['address'] == '0x00000001'
    s, e, _ = run('nested', 'nested', expected=0xc0000005)
    av = exceptions(e)
    assert len(av) >= 3 and av[0]['first_chance'] == av[1]['first_chance'] == 1
    assert av[0]['address'] != av[1]['address'] and av[-1]['first_chance'] == 0
    # This Windows WOW64 host reports STATUS_WX86_BREAKPOINT at process exit;
    # the actual first/second-chance debug exception codes remain STATUS_BREAKPOINT.
    s, e, _ = run('late-breakpoint', 'breakpoint', expected=0x4000001f)
    bp = exceptions(e)
    assert [x['first_chance'] for x in bp] == [1, 0] and all(x['code'] == '0x80000003' for x in bp)
    s, e, _ = run('partial', 'partial')
    target = exceptions(e)[0]['target_memory']
    assert target['requested'] == 32 and target['bounded'] == target['read'] == 8
    assert target['bytes'] == 'ab' * 8
    s, e, _ = run('module', 'module', args=[module])
    loaded = [x for x in e if x['kind'] == 'module_load' and x['path'].lower().endswith(module.name.lower())]
    assert len(loaded) == 1 and loaded[0]['size'] > 0
    assert any(x['kind'] == 'module_unload' and x['base'] == loaded[0]['base'] for x in e)
    values = ['', 'with spaces', 'quote"inside', 'trailing\\', 'slashes\\\\"quote', 'città 日本語']
    s, e, stdout = run('quotes', args=values)
    for i, value in enumerate(values):
        assert (f'arg{i}={value}\r\n').encode() in stdout, (i, stdout)
    s, e, stdout = run('handled-then-av', 'handled-then-av', expected=0xc0000005)
    assert [x['first_chance'] for x in exceptions(e)] == [1, 1, 0]
    assert b'handled_av_reached' in stdout
    unicode_dir = out / ('owned fixture città 日本語 ' + run_id)
    unicode_dir.mkdir()
    shutil.copy2(child, unicode_dir / child.name)
    original_child = child
    child = unicode_dir / child.name
    run('unicode-program-path', args=values)
    child = original_child
    s, e, stdout = run('stdin', 'stdin', input_data=b'owned fixture pipe only')
    assert b'fixture_stdin=owned fixture pipe only' in stdout
    for failure in ['context', 'metadata', 'safety', 'continue', 'output', 'cancel', 'budget', 'throw', 'throw-create', 'ntdll-throw', 'flush', 'duplicate', 'alias', 'resident']:
        s, e, _ = run('failure-' + failure, 'resident' if failure == 'resident' else 'churn', failure=failure)
        assert s['detached'] or s['exit_event_seen']
        if failure == 'context':
            startup = [x for x in e if x['kind'] == 'exception' and x['os_startup']]
            assert len(startup) == 1 and startup[0]['context'] is None and startup[0]['context_error'] != 0
        if failure == 'safety':
            assert s['detach_attempts'] >= 4
        if failure == 'metadata':
            assert s['pending_detach_attempts'] >= 1 and s['startup_consumed']
            assert any(x['kind'] == 'module_metadata' and x['system_ntdll'] for x in e)
        if failure == 'continue':
            assert s['pending_detach_attempts'] >= 1
        if failure == 'throw':
            assert len([x for x in e if x['kind'] == 'exception' and x['os_startup']]) == 1
            assert len([x for x in e if x['kind'] == 'process_create']) == 1
            assert s['pending_detach_attempts'] >= 1 and s['startup_consumed']
        if failure == 'throw-create':
            assert len([x for x in e if x['kind'] == 'process_create']) == 1
            assert s['pending_detach_attempts'] >= 1
        if failure == 'ntdll-throw':
            assert s['pending_detach_attempts'] >= 1 and s['detached']
            assert not s['startup_consumed'] and s['exit_code_unsigned'] == 37
        if failure == 'resident':
            assert not s['detached'] and s['exit_event_seen'] and s['attention_count'] >= 2
    def file_check(path, size, digest, expected):
        result = subprocess.run([str(observer), '--fixture-file-check', str(path), str(size), digest],
                                cwd=root, capture_output=True, timeout=10, creationflags=hidden)
        assert result.returncode == expected, (path, result.returncode, result.stderr)

    payload = child.read_bytes()
    digest = hashlib.sha256(payload).hexdigest()
    file_check(child, len(payload), digest, 0)
    file_check(child, len(payload) + 1, digest, 2)
    file_check(child, len(payload), '0' * 64, 2)
    invalid = out / (run_id + '-bad-pe.exe')
    invalid.write_bytes(b'bad' + payload[3:])
    file_check(invalid, invalid.stat().st_size, hashlib.sha256(invalid.read_bytes()).hexdigest(), 2)
    for short_length in range(64, 84):
        malformed = bytearray(b'MZ' + bytes(short_length - 2))
        struct.pack_into('<I', malformed, 0x3c, 0x10000000)
        invalid.write_bytes(malformed)
        file_check(invalid, short_length, hashlib.sha256(invalid.read_bytes()).hexdigest(), 2)
    fake_root = out / 'retired-gate'
    (fake_root / 'local').mkdir(parents=True)
    result = subprocess.run([str(observer), '--game-root', str(fake_root), '--output', str(fake_root / 'local/rejected')],
                            cwd=root, capture_output=True, timeout=10, creationflags=hidden)
    assert result.returncode == 2 and b'Historical R1 game capture is retired' in result.stderr
    assert not list((fake_root / 'local').glob('rejected*'))
    print('debug observer: owned-child/file-validation checks and retired game-mode rejection passed; no game execution')
