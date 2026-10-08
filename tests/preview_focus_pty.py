"""Preview capabilities, mouse isolation, footer and resize focus recovery."""
import fcntl
import os
from pathlib import Path
import signal
import struct
import tempfile
import termios
from display_pty import Terminal, Screen

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    (root/'00-directory').mkdir()
    (root/'01-short').write_text('short\n')
    (root/'02-binary').write_bytes(b'\0binary')
    (root/'03-empty').write_text('')
    (root/'04-long').write_text(''.join(f'LINE {i:03}\n' for i in range(40)))
    t = Terminal(directory,100,24)
    def focus(preview):
        assert (t.screen.rows[2][57]=='*') == preview
        assert (t.screen.rows[2][2]=='*') != preview
        assert ('Esc: Files' in t.screen.row(t.screen.height-1)) == preview
    def list_rows(): return [t.screen.row(y)[:55] for y in range(3,22)]
    try:
        for index in range(4):
            focus(False)
            assert 'Tab: Preview' not in t.screen.row(23)
            before = list_rows()
            t.send('\t\x1b[Z'); focus(False)
            t.click(60,12); focus(False)
            t.send('\x1b[<65;61;13M\x1b[<64;61;13M')
            assert list_rows()==before
            assert 'Row ' not in '\n'.join(t.screen.row(y)[55:] for y in range(3,22))
            t.send('\x1bOB')
        assert 'Tab: Preview' in t.screen.row(23)
        before = list_rows()
        t.click(60,12); focus(True)
        t.send('\x1bOB'); assert 'Row 2' in t.screen.row(20)
        assert list_rows()==before
        t.send('\x1bOP\x1b'); focus(True)  # Help restores preview focus.
        fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',80,100,0,0))
        t.screen = Screen(80,100)
        os.kill(t.proc.pid,signal.SIGWINCH); t.read()
        focus(False); assert 'Tab: Preview' not in t.screen.row(79)
        fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',24,100,0,0))
        t.screen = Screen(24,100)
        os.kill(t.proc.pid,signal.SIGWINCH); t.read()
        focus(False); assert 'Tab: Preview' in t.screen.row(23)
        t.send('\t'); focus(True)
        (root/'04-long').write_text('')
        t.send('\x1bOB')  # viewport change detects replacement contents
        focus(False); assert 'Tab: Preview' not in t.screen.row(23)
        assert list_rows()==before
        t.send('\x1b[<64;5;8M')  # list wheel continues to navigate
        assert list_rows()!=before
    finally:
        t.close()
print('PASS: short text/binary/empty file/directory Tab/click/wheel isolation; scroll footer/border/input; modal restore; resize/content loss; no automatic focus on resize completion; list wheel')

# Wrapping a static status on the minimum terminal does not create an action.
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    (root/'00-directory').mkdir()
    (root/'01-short').write_text('short\n')
    (root/'02-binary').write_bytes(b'\0binary')
    (root/'03-empty').write_text('')
    t = Terminal(directory,50,9)
    try:
        for index in range(4):
            t.send('\t\x1b[Z'); t.click(35,4)
            assert t.screen.rows[2][2]=='*' and t.screen.rows[2][32]==' '
            assert 'Tab: Preview' not in t.screen.row(8)
            before = [t.screen.row(y)[:30] for y in range(3,7)]
            t.send('\x1b[<65;36;5M')
            assert [t.screen.row(y)[:30] for y in range(3,7)]==before
            t.send('\x1bOB')
    finally:
        t.close()
print('PASS: static statuses stay unfocusable at minimum 50x9 geometry')
