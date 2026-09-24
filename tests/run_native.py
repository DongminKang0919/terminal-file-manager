"""Disposable filesystem fixtures for the UI-independent C tests."""
import os
from pathlib import Path
import subprocess
import tempfile

with tempfile.TemporaryDirectory(prefix='tfile-native-') as directory:
    root = Path(directory)
    (root / 'sub').mkdir()
    (root / 'sample.txt').write_text('first line\nsecond\tline\nthird line\n')
    (root / 'sub' / 'sample.txt').write_text('nested\n')
    (root / '.hidden').write_text('hidden\n')
    (root / 'binary.bin').write_bytes(b'abc\x00def')
    (root / 'alias').symlink_to(root / 'sub', target_is_directory=True)
    (root / 'dangling').symlink_to(root / 'missing-target')
    (root / 'existing.txt').write_text('keep me\n')
    (root / 'unsupported-tree').mkdir()
    os.mkfifo(root / 'unsupported-tree' / 'pipe')
    for i in range(120):
        (root / f'entry-{i:03d}').touch()
    locked = root / 'locked'
    locked.mkdir()
    permissions_test = os.geteuid() != 0
    if permissions_test:
        locked.chmod(0)
    try:
        subprocess.run([os.environ.get('TFILE_PLATFORM_TEST', './tests/platform_test'), directory] + (['permissions'] if permissions_test else []), check=True)
        subprocess.run([os.environ.get('TFILE_CORE_TEST', './tests/core_test'), directory], check=True)
    finally:
        locked.chmod(0o700)
