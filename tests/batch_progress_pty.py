"""Pipe-synchronized real batch modal cancellation and queue draining."""
import fcntl
import os
from pathlib import Path
import pty
import select
import signal
import struct
import subprocess
import tempfile
import termios
import time
from display_pty import Screen
binary=os.path.abspath('tests/tfile_batch')
cases=[('copy','partial','esc',80,24),('copy','partial','mouse',50,9),('delete','partial','esc',160,32),('delete','partial','mouse',50,9),('move','boundary','esc',80,24),('move','boundary','resize',50,9)]
for case,(action,gate,how,w,h) in enumerate(cases + [('copy','partial','esc',50,9),('move','boundary','resize',80,24),('delete','partial','mouse',160,32)]):
    dual=case>=len(cases)
    with tempfile.TemporaryDirectory(prefix='tfile-batch-gate-') as directory:
        root=Path(directory)/'source';dst=Path(directory)/'target';root.mkdir();dst.mkdir()
        for name in ['a','b','c']:
            if action=='delete':
                (root/name).mkdir()
                for i in range(4):(root/name/str(i)).write_bytes(b'x')
            else:(root/name).write_bytes(b'x'*262144)
        master,slave=pty.openpty();fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',h,w,0,0))
        er,ew=os.pipe();cr,cw=os.pipe()
        proc=subprocess.Popen([binary,str(root)],stdin=slave,stdout=slave,stderr=slave,pass_fds=(ew,cr),
            env={**os.environ,'TERM':'xterm-256color','LC_ALL':'C.UTF-8','TFILE_GATE_EVENT':str(ew),'TFILE_GATE_COMMAND':str(cr),'TFILE_BATCH_GATE':gate})
        os.close(slave);os.close(ew);os.close(cr);screen=Screen(h,w);events=bytearray()
        def body():return '\n'.join(screen.row(y) for y in range(screen.height))
        def pump(test):
            end=time.monotonic()+8
            while not test():
                assert time.monotonic()<end,(action,how,events,body())
                for fd in select.select([master,er],[],[],.05)[0]:
                    try:data=os.read(fd,65536)
                    except OSError:data=b''
                    assert data,(proc.poll(),events,body())
                    if fd==master:screen.feed(data)
                    else:events.extend(data)
        def send(text):os.write(master,text.encode())
        try:
            pump(lambda:'Shown:' in body())
            if dual:
                send('\x1b[20~'+'\x1bOB'*18+'\n');pump(lambda:'Left* /' in body())
                send('\t \t') # inactive Right has its own marked a
            send(' \x1bOB \x1bOB ');pump(lambda:'Sel:3' in body() or 'Selected: 3' in body() or (dual and 'M:3' in body()))
            send({'copy':'\x1b[15~','move':'\x1b[17~','delete':'\x1b[19~'}[action])
            if action!='delete':
                pump(lambda:'Batch destination' in body());send('\x1bOH'+'\x1b[3~'*len(str(root))+str(dst)+'\n')
            pump(lambda:'confirmation' in body());send('\t\n');pump(lambda:b'G\n' in events)
            pump(lambda:'Target 1/3' in body() or 'Target 2/3' in body())
            queued='q\x1b[15~\x1b[19~'+('\t' if dual else '')
            if how=='esc':queued+='\x1b'
            elif how=='mouse':
                rw=min(w-4,76);rx=(w-rw)//2;ph=min(7,h-2);py=(h-ph)//2
                queued+=f'\x1b[<0;{rx+5};{py+ph-1}M'
            else:
                screen=Screen(h+1,w+2);fcntl.ioctl(master,termios.TIOCSWINSZ,struct.pack('HHHH',h+1,w+2,0,0));os.kill(proc.pid,signal.SIGWINCH)
            send(queued);os.write(cw,b'x');pump(lambda:b'R ' in events)
            result=events.split(b'R ')[1].splitlines()[0].split()
            assert result[0]==b'11' and result[1]==b'1' and result[-1]==b'0',(action,how,dual,events,body())
            if action=='move':assert result[2]==b'1' and result[-3:-1]==[b'1',b'3'];assert (dst/'a').exists() and not (root/'a').exists()
            elif action=='copy':assert result[4]==b'65536' and (dst/'a').stat().st_size==65536
            else:assert result[3]==b'1' and len(list((root/'a').iterdir()))==3
            pump(lambda:'[Cancelled]' in body())
            assert proc.poll() is None and 'confirmation' not in body() and 'Batch destination' not in body()
            assert (root/'b').exists() and (root/'c').exists()
            if dual: assert 'Left* /' in body()
            send('!');pump(lambda:'Recent operation result' in body());send('\x1b');pump(lambda:'[Cancelled]' in body());send('q');assert proc.wait(timeout=3)==0
        finally:
            if proc.poll() is None:proc.kill();proc.wait()
            for fd in [master,er,cw]:os.close(fd)
print('PASS: single/dual pipe-gated batch copy/delete partial cancellation, move-between targets, mouse/Esc/resize, exact statuses/residue and queued background commands discarded')
