"""Use the PTY dimensions even when inherited LINES/COLUMNS are stale."""
import fcntl
import os
from pathlib import Path
import signal
import struct
import sys
import tempfile
import termios
from display_pty import Terminal, Screen

baseline='--baseline' in sys.argv
saved={key:os.environ.get(key) for key in ('LINES','COLUMNS')}
try:
    os.environ.update(LINES='24',COLUMNS='80')
    with tempfile.TemporaryDirectory(prefix='tfile-fullscreen-') as directory:
        (Path(directory)/'file').write_text('full terminal\n')
        t=Terminal(directory,140,40)
        try:
            if baseline:
                assert t.screen.rows[2][79]=='┐' and t.screen.rows[2][139]==' '
                print('REPRODUCED: stale COLUMNS=80 leaves 60 of 140 columns unused')
            else:
                def frames(width,height,dual=False):
                    split=width//2 if dual else width*3//5 if width<80 else width*11//20
                    t.frame(0,2,split,height-4)
                    t.frame(split,2,width-split,height-4)
                    assert t.screen.rows[height-3][width-1]=='┘'
                frames(140,40)
                # Full-width list, then both dual panels reach the right edge.
                t.send('\x1b[20~\x1bOH'+'\x1bOB'*17+'\n')
                t.frame(0,2,140,36)
                t.send('\x1b[20~\x1bOH'+'\x1bOB'*18+'\n')
                frames(140,40,True)
                for width,height in [(100,24),(160,42),(80,20)]:
                    t.screen=Screen(height,width)
                    fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',height,width,0,0))
                    t.proc.send_signal(signal.SIGWINCH);t.read()
                    try:frames(width,height,True)
                    except AssertionError:
                        print(width,height,'\n'.join(t.screen.row(y) for y in range(height)),flush=True)
                        raise
                print('PASS: actual terminal width/height at startup and resize; preview, full list and dual fill the terminal despite stale environment')
        finally:t.close()
finally:
    for key,value in saved.items():
        if value is None:os.environ.pop(key,None)
        else:os.environ[key]=value
