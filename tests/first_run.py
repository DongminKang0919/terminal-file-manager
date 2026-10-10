"""Clean first-run environment, absent optional tools and rejected non-TTY launches.
PTY output validates application behavior, not physical terminal graphics.
"""
import fcntl
import os
import select
import struct
import termios
import time
from pathlib import Path
import pty
import subprocess
import sys
import tempfile
from display_pty import BINARY, Screen, Terminal

class FirstScreen(Screen):
    def csi(self, params, command):
        if command != 'c':
            super().csi(params, command)

with tempfile.TemporaryDirectory(prefix='tfile-first-run-') as directory:
    base = Path(directory)
    root = base / 'files'
    root.mkdir()
    (base / 'bin').mkdir()  # No Vim, ImageMagick, Poppler or xdg-open.
    for name, data in [('00-text', b'text\n' * 80), ('empty', b''),
                       ('binary', b'\0binary'), ('picture.png', b'\x89PNG\r\n\x1a\ninvalid'),
                       ('page.pdf', b'%PDF-1.4\ninvalid')]:
        (root / name).write_bytes(data)
    clean = {'HOME': directory, 'XDG_CONFIG_HOME': str(base / 'config'),
             'XDG_DATA_HOME': str(base / 'data'), 'XDG_CACHE_HOME': str(base / 'cache'),
             'PATH': str(base / 'bin'), 'TERM': 'xterm-256color', 'LC_ALL': 'C.UTF-8'}
    # Either redirected stream must be rejected before emitting terminal controls.
    master, slave = pty.openpty()
    try:
        for input_fd, output_fd in [(subprocess.DEVNULL, subprocess.PIPE),
                                    (slave, subprocess.PIPE), (subprocess.DEVNULL, slave)]:
            result = subprocess.run([BINARY, str(root)], stdin=input_fd, stdout=output_fd,
                                    stderr=subprocess.PIPE, env=clean, timeout=2)
            assert result.returncode == 1 and b'interactive terminal' in result.stderr, result
            assert not result.stdout, result.stdout
        for term in [None, '', 'dumb']:
            environment = dict(clean)
            if term is None:
                environment.pop('TERM')
            else:
                environment['TERM'] = term
            result = subprocess.run([BINARY, str(root)], stdin=slave, stdout=slave,
                                    stderr=subprocess.PIPE, env=environment, timeout=2)
            assert result.returncode == 1 and b'Terminal type unavailable' in result.stderr, result
    finally:
        os.close(master)
        os.close(slave)
    saved = dict(os.environ)
    os.environ.clear()
    os.environ.update(clean)
    # Terminal's standard reader additionally ignores the Auto DA query.
    import display_pty
    display_pty.Screen = FirstScreen
    t = Terminal(str(root))
    def body():
        return '\n'.join(t.screen.row(y) for y in range(24))
    def find(name):
        t.send('\x06' + name + '\n')
    try:
        assert 'text' in body() and 'Settings (F7)' not in body(), body()
        t.send('e')
        assert 'Vim' in body() and 'install' in body().lower(), body()
        t.send('\x1b')
        t.send('\x1b[20~' + '\x1bOB' * 22 + '\n')
        assert 'Missing xdg-open' in body() and 'install xdg-utils' in body(), body()
        t.send('\x1b')
        (root / 'folder').mkdir()
        t.send('r')
        find('folder')
        t.send('\n')
        assert str(root / 'folder') in body(), body()
        t.send('\x7f')
        find('00-text')
        assert 'text' in body(), body()
        t.send('\x1bOQcreated\n')
        assert (root / 'created').is_file()
        t.send('\x1b[15~\t\x1bOH' + '\x1b[3~' * 100 + 'copied\n\n')
        assert (root / 'copied').is_file() and (root / 'created').is_file()
        find('copied')
        t.send('\x1b[17~\t\x1bOH' + '\x1b[3~' * 100 + 'moved\n\n')
        assert (root / 'moved').is_file() and not (root / 'copied').exists()
        find('created')
        # Mouse cancellation leaves the new file intact; explicit Delete removes it.
        t.send('\x1b[19~')
        for y in range(24):
            row = t.screen.row(y)
            if '[ Cancel ]' in row:
                t.click(row.index('[ Cancel ]') + 2, y)
                break
        else:
            raise AssertionError(body())
        assert (root / 'created').exists()
        t.send('\x1b[19~\t\n')
        assert not (root / 'created').exists()
        find('picture.png')
        t.send('\x1b[?62;1c\x1b[6;16;8t')  # A terminal that declares no Sixel.
        t.read()
        assert 'Missing ImageMagick' in body() and 'no Sixel' in body(), body()
        t.send('\t')
        assert 'Tab: Preview' not in body(), body()
        find('page.pdf')
        assert 'Missing pdftotext (Poppler)' in body(), body()
        find('binary')
        assert 'Preview unavailable' in body(), body()
        find('empty')
        assert 'Empty file' in body(), body()
        find('picture.png')
        t.send('\x1b[18~' + '\x1bOB' * 5 + '\n\x1b')
        assert 'Preview disabled' in body() and 'off' in body(), body()
        assert not (base / 'config/tfile/settings.conf').exists()
    finally:
        t.close()
        os.environ.clear()
        os.environ.update(saved)
    # README's no-argument invocation starts in the process working directory.
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 100, 0, 0))
    process = subprocess.Popen([BINARY], cwd=root, env=clean,
                               stdin=slave, stdout=slave, stderr=slave)
    os.close(slave)
    screen = FirstScreen(24, 100)
    try:
        deadline = time.monotonic() + 3
        while str(root) not in screen.row(1) or 'Shown:' not in screen.row(22):
            assert time.monotonic() < deadline, 'No-argument startup did not become ready'
            if select.select([master], [], [], .02)[0]:
                screen.feed(os.read(master, 65536))
        os.write(master, b'q')
        assert process.wait(timeout=3) == 0
        assert not (base / 'config/tfile/settings.conf').exists()
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)
    # The full-suite wrapper must remove inherited binary and graphics overrides.
    subprocess.run([sys.executable, 'tests/isolated_check.py', sys.executable, '-c',
                    'import os; assert not any(k.startswith("TFILE_") for k in os.environ); '
                    'assert os.environ["HOME"] != "poison-home"; '
                    'assert all(os.environ[k].startswith(os.environ["HOME"] + "/") '
                    'for k in ("XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"))'],
                   env={**saved, 'HOME': 'poison-home', 'TFILE_BINARY': '/missing',
                        'TFILE_SIXEL': '1', 'TFILE_CELL_PIXELS': '1x1'}, check=True)
print('PASS: clean HOME/XDG and no optional tools; create/delete/cancel, media reasons, '
      'Vim/xdg-open missing guidance, navigation/copy/move, no-argument startup, '
      'no unsolicited preview focus; non-TTY/TERM failures; suite environment isolation')
