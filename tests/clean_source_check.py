"""Build a source snapshot, check first run and installation in disposable roots.
This uses the host libraries/terminfo; it is not a fresh OS installation.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from install_check import check_install

root = Path(__file__).resolve().parents[1]
compiler = sys.argv[1] if len(sys.argv) > 1 else 'gcc'
if shutil.which(compiler) is None:
    raise SystemExit(f'Compiler unavailable: {compiler} (not verified)')
with tempfile.TemporaryDirectory(prefix='tfile-clean-source-') as directory:
    base = Path(directory)
    source = base / 'source'
    source.mkdir()
    paths = subprocess.check_output(['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=root).split(b'\0')
    for raw in paths:
        if not raw:
            continue
        relative = Path(os.fsdecode(raw))
        # Only source and test scripts; no git metadata, executable outputs or config.
        if relative == Path('Makefile') or (relative.parts[0] in ('src', 'tests') and
                                          relative.suffix in ('.c', '.h', '.py')):
            target = source / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(root / relative, target)
    # Exclude inherited build flags, loader overrides and application preferences.
    env = {'PATH': os.environ.get('PATH', os.defpath),
           'TERM': 'xterm-256color', 'LC_ALL': 'C.UTF-8'}
    env.update(HOME=str(base / 'user home'), XDG_CONFIG_HOME=str(base / 'config'),
               XDG_DATA_HOME=str(base / 'data'), XDG_CACHE_HOME=str(base / 'cache'))
    subprocess.run(['make', f'CC={compiler}'], cwd=source, env=env, check=True)
    subprocess.run([sys.executable, 'tests/cli_test.py'], cwd=source, env=env, check=True)
    # Isolate the version dependency: only version.h is newer than tfile.
    import re, time
    header=source/'src/version.h';original=header.read_text()
    stamp=time.time()-20
    for path in (source/'src').rglob('*'):
        if path.is_file():os.utime(path,(stamp,stamp))
    os.utime(source/'tfile',(stamp+10,stamp+10))
    header.write_text(re.sub(r'(#define TFILE_VERSION )"[^"]+"',r'\1"0.0.0-rebuild-test"',original))
    assert subprocess.run(['make','-q','tfile'],cwd=source,env=env).returncode==1
    subprocess.run(['make',f'CC={compiler}','tfile'],cwd=source,env=env,check=True)
    assert subprocess.check_output([str(source/'tfile'),'--version'],env={'PATH':''})==b'tfile 0.0.0-rebuild-test\n'
    header.write_text(original)
    subprocess.run(['make',f'CC={compiler}','tfile'],cwd=source,env=env,check=True)
    print('PASS: Git-free version output, version-only dependency rebuild and restoration',flush=True)
    subprocess.run([sys.executable, 'tests/first_run.py'], cwd=source, env=env, check=True)
    check_install(source, base, env)
print(f'PASS: clean source + install/uninstall, {compiler}, isolated HOME/XDG, no optional tools; host OS/libraries')
