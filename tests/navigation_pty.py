"""Loaded-list counts, viewport ranges, existing options and command group hitboxes."""
import fcntl
import os
from pathlib import Path
import signal
import struct
import tempfile
import termios
from display_pty import Terminal, Screen

# The VT oracle must scroll only its region and retain cursor/guard rows.
screen=Screen(5,4)
screen.rows=[list(c*4) for c in 'ABCDE']
screen.feed(b'\x1b[2;4r\x1b[3;2H\x1b[2S')
assert [screen.row(y) for y in range(5)]==['AAAA','DDDD','    ','    ','EEEE']
assert (screen.y,screen.x)==(2,1)
screen.feed(b'\x1b[T')
assert [screen.row(y) for y in range(5)]==['AAAA','    ','DDDD','    ','EEEE']
assert (screen.y,screen.x)==(2,1)

for w,h in [(50,9),(80,24),(100,24),(160,32)]:
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory)
        for i in range(60): (root/f'{i:02}-한글-긴파일이름.txt').write_text('body\n')
        (root/'.hidden').write_text('hidden\n')
        t=Terminal(directory,w,h)
        first=4 if h<12 else 5
        rows=h-7 if h<12 else h-8
        split=w*3//5 if w<80 else w*11//20
        def option(index):
            t.send('\x1b[18~'+'\x1bOB'*index+'\n')
            t.send('\x1b')
        try:
            assert 'Shown: 61 | Marked: 0 | Dotfiles: on' in t.screen.row(h-2)
            assert f'Shown 1-{rows}/61' in t.screen.row(h-3)
            t.send('\x1bOF')
            before=[t.screen.row(y)[:split] for y in range(first,h-2)]
            assert f'Shown {62-rows}-61/61' in t.screen.row(h-3)
            option(1)
            t.frame(0,2,w,h-4)
            assert 'Preview' not in t.screen.row(2)
            assert t.screen.rows[h-4][1]=='>'
            assert '59-' in t.screen.row(h-4)
            assert f'Shown {62-rows}-61/61' in t.screen.row(h-3)
            option(1)
            assert [t.screen.row(y)[:split] for y in range(first,h-2)]==before
            option(0)
            assert 'Shown: 60 | Marked: 0 | Dotfiles: off' in t.screen.row(h-2)
            assert '/60' in t.screen.row(h-3)
            option(0)
            assert 'Shown: 61 | Marked: 0 | Dotfiles: on' in t.screen.row(h-2)
            t.send('\x1b[19~\x1b')
            assert '[Cancelled]' in t.screen.row(h-2)
            t.send('\x1bOA')
            assert '[Cancelled]' in t.screen.row(h-2)
            # Gaps (including wide group gaps) have no hidden button targets.
            top=t.screen.row(0)
            for x in [i+1 for i,c in enumerate(top[:-1]) if c==']']:
                t.click(x,0)
                assert '[Cancelled]' in t.screen.row(h-2)
            # Enlarging preserves top; off-screen rows above still need a range.
            fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',80,w,0,0))
            t.screen=Screen(80,w)
            os.kill(t.proc.pid,signal.SIGWINCH); t.read()
            assert '/61' in t.screen.row(77)
            t.send('\x1bOH')
            assert 'Shown' not in t.screen.row(77)
            t.send('\x1bOF')
            # Resize to minimum height and 50/51 columns; the selected item stays visible.
            fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',9,51 if w==50 else 50,0,0))
            t.screen=Screen(9,51 if w==50 else 50)
            os.kill(t.proc.pid,signal.SIGWINCH); t.read()
            assert '/61' in t.screen.row(6)
            assert '[Cancelled]' in t.screen.row(7)
        finally: t.close()
    with tempfile.TemporaryDirectory() as directory:
        t=Terminal(directory,w,h)
        try:
            assert 'Shown: 0 | Marked: 0 | Dotfiles: on' in t.screen.row(h-2)
            assert 'Shown' not in t.screen.row(h-3)
            (Path(directory)/'single').write_text('one\n')
            t.send('r')
            assert 'Refreshed' in t.screen.row(h-2)
            assert 'Shown' not in t.screen.row(h-3)
        finally: t.close()
print('PASS: loaded/hidden counts, overflow ranges, empty/fitting list, Korean names, full-width preview toggle identity/scroll, retained cancellation, group gaps and resize')
