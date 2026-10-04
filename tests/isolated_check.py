"""Keep the entire regression suite independent of real startup preferences."""
import os
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="tfile-check-settings-") as directory:
    environment = {**os.environ, "HOME": directory,
                   "XDG_CONFIG_HOME": os.path.join(directory, "config")}
    # Preserve recursive make's jobserver descriptors.
    sys.exit(subprocess.call(sys.argv[1:], env=environment, close_fds=False))
