"""Held read and mutation-conflict lifetime boundaries, without releasing on input."""
import fcntl
import struct
import termios
import os
from pathlib import Path
import signal
import sys
import tempfile
import time
import display_pty
from preview_latency_pty import until, text
from process_probe import process_name

F3='\x1bOR'; F5='\x1b[15~'; F10=b'\x1b[21~'
CASE=sys.argv[1] if len(sys.argv)>1 else 'all'

def cleanup(t):
    if t.proc.poll() is None:
        for _ in range(4):t.send('\x1b')
        t.close()
    else:os.close(t.master)

for width,height in [(50,9),(100,24)]:
    for action in ['directory-exit','search-exit','search-results-exit','conflict-exit','conflict-crash','conflict-cancel','conflict-mouse','conflict-resize','conflict-term']:
        if CASE not in ['all',action]:continue
        with tempfile.TemporaryDirectory(prefix='tfile-boundary-') as temp:
            base=Path(temp); root=base/'root';root.mkdir();dest=base/'dest';dest.mkdir()
            config=base/'config/tfile';config.mkdir(parents=True)
            (config/'settings.conf').write_text('version=1\nimage=0\n')
            os.environ.update(HOME=str(base/'home'),XDG_CONFIG_HOME=str(base/'config'),
                              XDG_DATA_HOME=str(base/'data'),XDG_CACHE_HOME=str(base/'cache'))
            (base/'home').mkdir()
            gate=base/'gate';marker=base/'gate.entered'
            read_action=action.startswith(('directory','search'))
            display_pty.BINARY=os.path.abspath(os.environ.get('TFILE_BOUNDARY_READ_BINARY','tests/tfile_read') if read_action else os.environ.get('TFILE_BOUNDARY_BINARY','tfile'))
            t=None;worker=None
            try:
                if read_action:
                    (root/'child').mkdir()
                    os.environ['TFILE_SCAN_GATE']=str(gate)
                    t=display_pty.Terminal(str(root),width,height)
                    gate.write_text(str(root/'child' if action.startswith('directory') else root))
                    os.write(t.master,b'\n' if action.startswith('directory') else (F3+'child\n').encode())
                    until(t,lambda:marker.exists() and marker.read_text().strip())
                    worker=int(marker.read_text());assert worker!=t.proc.pid
                    until(t,lambda:('Loading directory' if action.startswith('directory') else 'Searching') in text(t))
                    if action=='search-results-exit':
                        gate.unlink();until(t,lambda:'Complete:' in text(t))
                else:
                    for name in ['a','b']:(root/name).write_text(name)
                    (dest/'a').write_text('do not overwrite')
                    t=display_pty.Terminal(str(root),width,height)
                    t.send('a'+F5+'\x1bOH'+'\x1b[3~'*600+str(dest)+'\n')
                    t.send('\t\n');until(t,lambda:'Name collision' in text(t))
                    assert 'F10: exit' in text(t),text(t)
                    children=Path(f'/proc/{t.proc.pid}/task/{t.proc.pid}/children').read_text().split()
                    worker=next(int(x) for x in children if process_name(x)=='tfile-mutation')
                start=time.monotonic()
                if action=='conflict-crash':
                    os.kill(worker,signal.SIGKILL)
                    until(t,lambda:'uncertain' in text(t).lower(),.6)
                    until(t,lambda:not Path(f'/proc/{worker}').exists(),.6)
                    t.send('!');until(t,lambda:'Uncertain' in text(t))
                    t.send('\x1b')
                elif action in ['conflict-cancel','conflict-mouse','conflict-resize']:
                    if action=='conflict-cancel':os.write(t.master,b'\x1b')
                    elif action=='conflict-mouse':
                        row=next(y for y in range(height) if 'Name collision' in t.screen.row(y))
                        # Click the actual popup close control, not the background.
                        t.click(t.screen.row(row).index('[ x ]')+2,row)
                    else:
                        t.screen=display_pty.Screen(height+1,width+1)
                        fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',height+1,width+1,0,0))
                        t.proc.send_signal(signal.SIGWINCH)
                    until(t,lambda:'Marked: 2' in text(t) and '[ Cancel ]' not in text(t) and not Path(f'/proc/{worker}').exists(),.6)
                    assert 'Marked: 2' in text(t),text(t)
                    t.send('!');until(t,lambda:'Cancelled' in text(t));t.send('\x1b')
                else:
                    if action=='conflict-term':t.proc.send_signal(signal.SIGTERM)
                    else:os.write(t.master,F10)
                    until(t,lambda:t.proc.poll() is not None,.6)
                    assert t.proc.returncode==0
                    assert not Path(f'/proc/{worker}').exists()
                if not read_action:
                    assert (dest/'a').read_text()=='do not overwrite'
                    assert not (dest/'b').exists() and (root/'a').read_text()=='a' and (root/'b').read_text()=='b'
                print(f'PASS {action} {width}x{height}: {(time.monotonic()-start)*1000:.1f}ms; worker reaped, data unchanged',flush=True)
            finally:
                gate.unlink(missing_ok=True)
                os.environ.pop('TFILE_SCAN_GATE',None)
                if t:cleanup(t)
