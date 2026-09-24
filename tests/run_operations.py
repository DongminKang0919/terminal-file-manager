"""Native operation tests must enforce permissions; all data stays disposable."""
import os
import pwd
import subprocess
import tempfile

binary = os.path.abspath(os.environ.get('TFILE_OPERATIONS_TEST', './tests/operations_test'))
with tempfile.TemporaryDirectory(prefix='tfile-operations-') as directory:
    kwargs = {}
    if os.geteuid() == 0:
        # Drop privileges for the entire child, including all fixture creation.
        nobody = pwd.getpwnam('nobody')
        os.chown(directory, nobody.pw_uid, nobody.pw_gid)
        kwargs = dict(user=nobody.pw_uid, group=nobody.pw_gid, extra_groups=[])
    subprocess.run([binary, directory], check=True, **kwargs)
