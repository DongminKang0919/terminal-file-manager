"""Second-pass regressions; enforce non-root permission checks in disposable trees."""
import os
from pathlib import Path
import pwd
import subprocess
import sys
import tempfile

targets = {'search': ('TFILE_SEARCH_TEST', './tests/search_test'),
           'controller': ('TFILE_CONTROLLER_TEST', './tests/controller_test')}
for target in sys.argv[1:] or targets:
    variable, default = targets[target]
    binary = os.path.abspath(os.environ.get(variable, default))
    with tempfile.TemporaryDirectory(prefix='tfile-regressions-') as directory:
        root = Path(directory)
        for name in ('locked', 'broken'):
            (root / name).mkdir()
        (root / 'found.txt').write_text('found preview\n')
        (root / '.hidden').write_text('hidden preview\n')
        (root / 'vanished').write_text('metadata race\n')
        kwargs = {}
        if os.geteuid() == 0:
            nobody = pwd.getpwnam('nobody')
            for item in [root, *root.iterdir()]:
                os.chown(item, nobody.pw_uid, nobody.pw_gid)
            kwargs = dict(user=nobody.pw_uid, group=nobody.pw_gid, extra_groups=[])
        try:
            subprocess.run([binary, directory], check=True, **kwargs)
        finally:
            root.chmod(0o700)
            (root / 'locked').chmod(0o700)
