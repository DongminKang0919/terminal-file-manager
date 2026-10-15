"""Cancellation acknowledgement, resize, deferred exit and lost-parent policy."""
import ctypes
import fcntl
import os
from pathlib import Path
import signal
import struct
import tempfile
import time
import termios
import display_pty
from preview_latency_pty import until,text,pump

display_pty.BINARY=os.path.abspath(os.environ.get('TFILE_MUTATION_BINARY','tests/tfile_operation_latency'))
assert ctypes.CDLL(None).prctl(36,1,0,0,0)==0 # Reap a worker orphaned by the intentional UI SIGKILL.
for action in ['resize','exit','cancel-then-exit','int','term','hup','crash-worker','kill-parent']:
    with tempfile.TemporaryDirectory(prefix='mutation-shutdown-') as temp:
        root=Path(temp)/'root';root.mkdir();source=root/'source';source.write_bytes(b'x'*1048576)
        log=Path(temp)/'result';marker=Path(str(log)+'.entered')
        os.environ.update(TFILE_OPERATION_LOG=str(log),TFILE_OPERATION_PHASE='read')
        t=display_pty.Terminal(str(root))
        worker=None
        try:
            t.send('\x1b[15~');t.send('\t\x1bOHcopy-\n');os.write(t.master,b'\n')
            until(t,lambda:marker.exists() and len(marker.read_text().split())==2)
            worker=int(marker.read_text().split()[-1]);assert worker!=t.proc.pid
            started=time.monotonic()
            if action=='kill-parent':
                t.proc.kill();assert t.proc.wait(timeout=.5)==-signal.SIGKILL
                deadline=time.monotonic()+2
                while True:
                    found,status=os.waitpid(worker,os.WNOHANG)
                    if found:break
                    assert time.monotonic()<deadline;time.sleep(.01)
                assert os.WIFEXITED(status) and os.WEXITSTATUS(status)==0
                assert (root/'copy-source').stat().st_size==0
                print(f'{action}: orphan safely stopped/reaped, retained empty partial file',flush=True)
                continue
            if action=='crash-worker':os.kill(worker,signal.SIGKILL)
            elif action in ['int','term','hup']:t.proc.send_signal({'int':signal.SIGINT,'term':signal.SIGTERM,'hup':signal.SIGHUP}[action])
            elif action=='resize':
                t.screen=display_pty.Screen(8,49)
                fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',8,49,0,0));t.proc.send_signal(signal.SIGWINCH)
            else:os.write(t.master,b'\x1b' if action=='cancel-then-exit' else b'\x1b[21~')
            if action!='crash-worker':
                until(t,lambda:'Cancellation requested' in text(t),.4)
                ack=(time.monotonic()-started)*1000
                assert not log.exists() and t.proc.poll() is None
                if action=='cancel-then-exit':os.write(t.master,b'\x1b[21~')
                if action=='resize':
                    t.screen=display_pty.Screen(24,100)
                    fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',24,100,0,0));t.proc.send_signal(signal.SIGWINCH)
                until(t,lambda:log.exists() and bool(log.read_text().strip()),2)
                assert log.read_text().split()[2]=='11',log.read_text()
                until(t,lambda:not Path(f'/proc/{worker}').exists())
                elapsed=(time.monotonic()-started)*1000
                assert elapsed>750 and (root/'copy-source').stat().st_size==0
                if action!='resize':assert t.proc.wait(timeout=1)==0
                print(f'{action}: acknowledgement={ack:.1f}ms actual_exit={elapsed:.1f}ms',flush=True)
            else:
                until(t,lambda:'uncertain' in text(t).lower())
                t.send('\x1b');t.send('!');until(t,lambda:'Uncertain' in text(t))
                assert 'source' in text(t) and 'copy-source' in text(t)
                t.send('\x1b')
                assert not Path(f'/proc/{worker}').exists()
                print('crash-worker: uncertain result and both inspectable paths, child reaped',flush=True)
        finally:
            if t.proc.poll() is None:t.close()
            else:os.close(t.master)
            os.environ.pop('TFILE_OPERATION_LOG',None);os.environ.pop('TFILE_OPERATION_PHASE',None)
