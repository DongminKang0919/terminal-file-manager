"""Destination edit/review/picker loops leave files and marks alone until Execute."""
import fcntl
import os
from pathlib import Path
import signal
import struct
import tempfile
import termios
from display_pty import Terminal

F5='\x1b[15~'; HOME='\x1bOH'; DELETE='\x1b[3~'
for w,h in [(50,9),(80,24),(160,32)]:
    with tempfile.TemporaryDirectory() as directory:
        base=Path(directory); source=base/'source'; source.mkdir()
        for name in ['a','b']: (source/name).write_text(name)
        dest=base/'한글-dest'; dest.mkdir()
        long=dest
        for i in range(3):
            long=long/('long'+str(i)+'한'*25); long.mkdir()
        t=Terminal(str(source),w,h)
        def body(): return '\n'.join(t.screen.row(y) for y in range(h))
        def replace(value): t.send(HOME+DELETE*600+str(value))
        def button(label):
            for y in range(h):
                row=t.screen.row(y)
                if label in row: t.click(row.index(label)+2,y); return
            raise AssertionError(body())
        def unchanged():
            assert [(p.name,p.read_text()) for p in sorted(source.iterdir())]==[('a','a'),('b','b')]
            assert not (dest/'a').exists() and not (dest/'b').exists()
        try:
            t.send(' \x1bOB '+F5)
            assert 'Base:' in body() and 'Relative paths' in body()
            replace('missing'); t.send('\n'); assert 'Destination:' in body(); unchanged()
            # Cursor stays at the end after validation: backspace repairs the value.
            t.send('\x7f'*7+'a\n'); assert 'Destination:' in body(); unchanged()
            replace('../한글-dest'); t.send('\t\n')
            assert 'Choose destination directory' in body() and '한글-dest' in body()
            t.send('\x1b'); assert '../한글-dest' in body()
            t.send('\n'); assert 'Choose destination directory' in body() # Browse focus retained
            t.send(' '); assert 'Batch destination' in body()
            t.send('\x1b[Z\n'); assert 'Batch copy confirmation' in body(); unchanged()
            t.send('\n'); assert 'Batch destination' in body() # default Cancel returns
            replace('missing'); button('[ Browse ]')
            assert 'Choose destination directory' in body() and 'source' in body()
            t.send('\x1b'); assert 'missing' in body(); unchanged()
            t.send('\x1b[Z'); replace(str(long)); t.send('\n')
            assert 'Batch copy confirmation' in body(); t.send('\x1b')
            assert 'Batch destination' in body(); button('[ Cancel ]'); unchanged()
            # Resize each nesting level; no stale form/confirmation can execute.
            for nesting in ['form','picker','picker-path','review']:
                t.send(F5)
                if nesting in ['picker','picker-path']: button('[ Browse ]')
                if nesting=='picker-path': t.send('p')
                if nesting=='review': replace(str(dest)); t.send('\n')
                fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',h,w,0,0))
                os.kill(t.proc.pid,signal.SIGWINCH); t.read()
                assert 'Batch destination' not in body() and 'confirmation' not in body() and 'Choose destination' not in body()
                unchanged()
            if os.geteuid()!=0:
                dest.chmod(0)
                try:
                    t.send(F5); replace(str(dest)); t.send('\n')
                    assert 'Permission denied' in body()
                finally: dest.chmod(0o700)
                t.send('\n')
            else:
                print('SKIP: inaccessible destination (root)')
                t.send(F5); replace(str(dest)); t.send('\n')
            assert 'Batch copy confirmation' in body(); t.send('\t\n')
            assert (dest/'a').read_text()=='a' and (dest/'b').read_text()=='b'
        finally: t.close()
print('PASS: batch destination errors/edit/picker/review/cancel/resize/Unicode paths at all sizes')
