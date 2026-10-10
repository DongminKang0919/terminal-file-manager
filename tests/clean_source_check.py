"""Build tracked source only, with disposable settings and absent optional tools.
This uses the host libraries/terminfo; it is not a fresh OS installation.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
compiler = sys.argv[1] if len(sys.argv) > 1 else 'gcc'
if shutil.which(compiler) is None:
    raise SystemExit(f'Compiler unavailable: {compiler} (not verified)')
with tempfile.TemporaryDirectory(prefix='tfile-clean-source-') as directory:
    base = Path(directory)
    source = base / 'source'
    source.mkdir()
    paths = subprocess.check_output(['git', 'ls-files', '-z'], cwd=root).split(b'\0')
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
    env.update(HOME=str(base / 'home'), XDG_CONFIG_HOME=str(base / 'config'),
               XDG_DATA_HOME=str(base / 'data'), XDG_CACHE_HOME=str(base / 'cache'))
    subprocess.run(['make', f'CC={compiler}'], cwd=source, env=env, check=True)
    subprocess.run([sys.executable, 'tests/first_run.py'], cwd=source, env=env, check=True)
print(f'PASS: clean tracked source, {compiler}, isolated HOME/XDG, no optional tools')
