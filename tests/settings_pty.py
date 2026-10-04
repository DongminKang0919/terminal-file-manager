"""Persist preferences across real processes and keep startup warnings visible."""
import os
from pathlib import Path
import tempfile
from display_pty import Terminal

DOWN = '\x1bOB'
F7 = '\x1b[18~'
F9 = '\x1b[20~'
for width, height in [(50, 9), (100, 24)]:
    with tempfile.TemporaryDirectory(prefix='tfile-settings-pty-') as directory:
        root = Path(directory)
        start = root / 'start'
        start.mkdir()
        (start / '.hidden').write_text('hidden')
        (start / 'visible').write_text('visible')
        os.environ.update(HOME=directory, XDG_CONFIG_HOME=str(root / 'config'))
        path = root / 'config/tfile/settings.conf'
        t = Terminal(str(start), width, height)
        try:
            t.send(F9 + DOWN * 18 + '\n' + '\t')
            assert 'Right*' in t.screen.row(height - 2)
            t.send(F7 + '\n' + DOWN * 2 + '\n' + DOWN + '\n' + DOWN + '\n' + DOWN + '\n')
            assert not path.exists()  # Changing options alone never writes.
            t.send(DOWN + '\n')
            assert 'Saved startup defaults' in '\n'.join(t.screen.row(y) for y in range(height))
            t.send('\x1b')
        finally:
            t.close()
        text = path.read_text()
        for value in ['mode=2', 'sort=1', 'descending=1', 'hidden=0', 'wheel=3', 'image=0']:
            assert value in text, text
        t = Terminal(str(start), width, height)
        try:
            body = '\n'.join(t.screen.row(y) for y in range(height))
            assert 'Left*' in body and 'Size-' in body and 'H:0' in body, body
            assert 'visible' in body and '.hidden' not in body, body
            t.send('\t')
            assert 'Right*' in t.screen.row(height - 2)
            t.send(F7)
            body = '\n'.join(t.screen.row(y) for y in range(height))
            assert 'Show hidden files' in body
        finally:
            t.close()
        path.write_text('version=2\n')
        t = Terminal(str(start), width, height)
        try:
            assert 'Settings (F7)' in t.screen.row(height - 2)
            t.send('r')
            assert 'Settings (F7)' in t.screen.row(height - 2)
            t.send(F7 + DOWN * 6 + '\n')
            assert path.read_text() == 'version=2\n'
            assert 'Confirm: replace' in '\n'.join(t.screen.row(y) for y in range(height))
            t.send('\x1b')
            assert path.read_text() == 'version=2\n'
            t.send(F7 + DOWN * 6 + '\n\n')
            assert path.read_text().startswith('version=1\n')
        finally:
            t.close()
print('PASS: real-process settings save/restart, dual CLI path, startup warning and explicit replacement at 50x9/100x24')
