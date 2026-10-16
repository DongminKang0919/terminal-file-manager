"""Installation checks, called only with clean_source_check's disposable copy.
No user/system installation, package installation or shell configuration changes.
"""
import hashlib
import os
import re
from pathlib import Path
import shutil
import stat
import subprocess
import sys


def check_install(source, base, env):
    source, base = Path(source), Path(base)
    assert source == base / 'source' and base.name.startswith('tfile-clean-source-')
    assert source.is_dir() and not (source / '.git').exists()
    outside = base / 'other working directory'
    outside.mkdir()
    prefix = Path(env['HOME']) / '.local'
    stage = base / 'stage root'
    staged = stage / 'usr/local/bin/tfile'
    installed = prefix / 'bin/tfile'
    version = re.search(r'#define TFILE_VERSION "([^"]+)"', (source/'src/version.h').read_text())[1]
    expected = hashlib.sha256((source / 'tfile').read_bytes()).hexdigest()
    makefile = base / 'uninstall.mk'
    shutil.copyfile(source / 'Makefile', makefile)

    def make(*args, detached=False):
        command = ['make', '-f', str(makefile)] if detached else ['make', '-C', str(source)]
        subprocess.run(command + list(args), cwd=outside, env=env, check=True, umask=0o077)

    def executable(path):
        assert path.is_file() and not path.is_symlink(), path
        assert stat.S_IMODE(path.stat().st_mode) == 0o755, oct(path.stat().st_mode)
        assert hashlib.sha256(path.read_bytes()).hexdigest() == expected
        assert stat.S_IMODE(path.parent.stat().st_mode) == 0o755

    # README user-prefix recipe; spaces test Makefile quoting, restrictive umask
    # verifies permissions are deliberate rather than inherited by accident.
    make('install', 'PREFIX=' + str(prefix))
    executable(installed)
    assert sorted(p.relative_to(prefix).as_posix() for p in prefix.rglob('*') if p.is_file()) == ['bin/tfile']
    make('install', 'PREFIX=' + str(prefix))  # Reinstall before removing source.
    executable(installed)
    make('uninstall', 'PREFIX=' + str(prefix))
    assert not installed.exists()
    make('install', 'PREFIX=' + str(prefix))
    executable(installed)
    # Omit PREFIX to test the actual /usr/local default, safely under DESTDIR.
    make('install', 'DESTDIR=' + str(stage))
    executable(staged)
    assert sorted(p.relative_to(stage).as_posix() for p in stage.rglob('*') if p.is_file()) == ['usr/local/bin/tfile']
    # Also exercise explicit PREFIX combined with DESTDIR.
    make('install', 'DESTDIR=' + str(stage), 'PREFIX=/opt/tfile test')
    custom = stage / 'opt/tfile test/bin/tfile'
    executable(custom)
    make('uninstall', 'DESTDIR=' + str(stage), 'PREFIX=/opt/tfile test')
    assert not custom.exists() and staged.is_file() and installed.is_file()

    # Keep only Python test drivers, not any product source/build/resource file.
    harness = base / 'test drivers'
    harness.mkdir()
    for path in (source / 'tests').glob('*.py'):
        shutil.copyfile(path, harness / path.name)
    shutil.rmtree(source)
    assert not source.exists()
    run_env = {**env, 'TFILE_BINARY': str(installed), 'TFILE_EXPECTED_VERSION': version}
    assert str(installed.parent) not in env['PATH'].split(os.pathsep)
    path_env = {**env, 'PATH': str(installed.parent) + os.pathsep + env['PATH']}
    assert shutil.which('tfile', path=path_env['PATH']) == str(installed)
    result = subprocess.run(['tfile', 'one', 'two'], cwd=outside, env=path_env,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=3)
    assert result.returncode == 1 and b'Usage:' in result.stderr
    for test in ['cli_test.py', 'first_run.py', 'settings_pty.py', 'media_pty.py', 'terminal_auto_pty.py']:
        subprocess.run([sys.executable, str(harness / test)], cwd=outside, env=run_env, check=True)
        print('PASS: installed, source removed, unrelated cwd: ' + test, flush=True)
    subprocess.run([sys.executable, str(harness / 'first_run.py')], cwd=outside,
                   env={**env, 'TFILE_BINARY': str(staged)}, check=True)
    subprocess.run([sys.executable,str(harness/'cli_test.py')],cwd=outside,
                   env={**env,'TFILE_BINARY':str(staged),'TFILE_EXPECTED_VERSION':version},check=True)
    print('PASS: staged binary first run from unrelated cwd, source removed', flush=True)
    # Run the staged binary too; no assumption that DESTDIR is a runtime root.
    result = subprocess.run([str(staged), 'one', 'two'], cwd=outside, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=3)
    assert result.returncode == 1 and b'Usage:' in result.stderr and not result.stdout
    assert str(source).encode() not in result.stderr

    protected = [prefix / 'bin/unrelated', prefix / 'marker',
                 Path(env['HOME']) / '.config/tfile/settings.conf',
                 Path(env['XDG_CONFIG_HOME']) / 'tfile/settings.conf',
                 Path(env['XDG_CONFIG_HOME']) / 'tfile/favorites.conf',
                 Path(env['XDG_DATA_HOME']) / 'user-file',
                 Path(env['XDG_CACHE_HOME']) / 'marker', stage / 'marker']
    for path in protected:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b'preserve user data\n')
    for _ in range(2):
        make('uninstall', 'PREFIX=' + str(prefix), detached=True)
        make('uninstall', 'DESTDIR=' + str(stage), detached=True)
        assert not installed.exists() and not staged.exists()
        assert all(path.read_bytes() == b'preserve user data\n' for path in protected)
    assert (prefix / 'bin').is_dir() and (stage / 'usr/local/bin').is_dir()
    print('PASS: user PREFIX/spaces/0755/reinstall, default+explicit DESTDIR exact layout, '
          'source-independent UI/settings/help/media, idempotent uninstall preserves HOME/XDG/unrelated files/directories', flush=True)
