"""Disposable home Trash, actual PTY input/results; no pixel or desktop restore claim."""
import os
from pathlib import Path
import tempfile
import fcntl
import struct
import termios
import signal
from display_pty import Terminal, Screen

def screen(t):
    return '\n'.join(t.screen.row(i) for i in range(t.screen.height))

for width,height in [(50,9),(100,24)]:
    with tempfile.TemporaryDirectory(prefix='tfile-trash-pty-') as d:
        base=Path(d); root=base/'root';root.mkdir()
        data=base/'data';os.environ['XDG_DATA_HOME']=str(data)
        os.environ['XDG_CONFIG_HOME']=str(base/'config')
        a=root/'a 한글';b=root/'b';a.write_text('a');b.write_text('b')
        t=Terminal(str(root),width=width,height=height)
        try:
            t.send('t');assert 'Confirm Trash' in screen(t)
            t.send('\n');assert a.exists() and b.exists()  # default Cancel
            t.send('t\t\n');assert not a.exists() and b.exists()
            payloads=list((data/'Trash/files').iterdir()); assert len(payloads)==1 and payloads[0].read_text()=='a'
            info=list((data/'Trash/info').iterdir());assert len(info)==1 and '%ED%95%9C%EA%B8%80' in info[0].read_text()
            # Marked batch remains frozen and cancellation moves nothing.
            a.write_text('a2');t.send('r\x1bOH');t.send(' ');t.send('\x1bOB ');t.send('t')
            assert 'Batch Trash confirmation' in screen(t), screen(t)
            t.send('\n');assert a.exists() and b.exists()
            t.send('t\t\n');assert not a.exists() and not b.exists()
            assert len(list((data/'Trash/files').iterdir()))==3
            a.write_text('keep');t.send('r');t.send('t\x1b');assert a.exists()
            # Permanent delete is still a separate warning and command.
            t.send('\x1b[19~');assert 'Confirm deletion' in screen(t)
            t.send('\t\n');assert not a.exists() and len(list((data/'Trash/files').iterdir()))==3
            # An unsafe Trash folder refuses rather than deleting the original.
            a.write_text('keep');(data/'Trash').chmod(0o755);t.send('r'+'t\t\n')
            assert a.read_text()=='keep'
            t.send('!');assert 'Original kept' in screen(t),screen(t)
            t.send('\x1b')
            (data/'Trash').chmod(0o700)
            t.send('t');assert 'Confirm Trash' in screen(t)
            t.screen=Screen(height,width+1)
            fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',height,width+1,0,0))
            os.kill(t.proc.pid,signal.SIGWINCH);t.read();assert a.exists() and 'Confirm Trash' not in screen(t)
            if width==100:
                child=root/'child';child.mkdir();target=child/'only-right';target.write_text('right')
                t.send('r\x1b[20~'+'\x1bOB'*18+'\n\t\x1bOH\n')
                t.send('t\t\n');assert not target.exists() and a.read_text()=='keep'
        finally:t.close()
print('PASS: Trash/default cancel/frozen marks/batch/no-delete fallback, separate permanent delete, resize cancellation at 50x9/100x24 (PTY)')
