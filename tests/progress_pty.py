"""Real modal input with pipe-gated callbacks, not disk speed or arbitrary delays."""
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

binary = os.path.abspath(os.environ.get('TFILE_PROGRESS_BINARY', 'tests/tfile_progress'))
for copy, action, width, height in [(True,'esc',100,24),(True,'mouse',50,9),(True,'resize',100,24),
                                   (False,'esc',100,24),(False,'mouse',50,9),(False,'resize',100,24)]:
    with tempfile.TemporaryDirectory(prefix='tfile-progress-pty-') as directory:
        root=Path(directory); source=root/'source'; target=root/'copy-source'
        if copy: source.write_bytes(b'x'*1048576)
        else:
            source.mkdir()
            for i in range(8): (source/str(i)).write_text('keep')
        master,slave=pty.openpty()
        fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',height,width,0,0))
        event_r,event_w=os.pipe(); command_r,command_w=os.pipe()
        proc=subprocess.Popen([binary,directory],stdin=slave,stdout=slave,stderr=slave,
            pass_fds=(event_w,command_r),env={**os.environ,'TERM':'xterm-256color','LC_ALL':'C.UTF-8',
                'TFILE_GATE_EVENT':str(event_w),'TFILE_GATE_COMMAND':str(command_r)})
        os.close(slave); os.close(event_w); os.close(command_r)
        output=bytearray(); events=bytearray()
        def pump_until(predicate):
            deadline=time.monotonic()+8
            while not predicate():
                assert time.monotonic()<deadline, (copy,action,bytes(output[-4000:]),bytes(events))
                for fd in select.select([master,event_r],[],[],.05)[0]:
                    try: data=os.read(fd,65536)
                    except OSError: data=b''
                    assert data, (proc.poll(),bytes(output[-4000:]))
                    (output if fd==master else events).extend(data)
        def send(data): os.write(master,data)
        try:
            pump_until(lambda:b'F1' in output)
            if copy:
                send(b'\x1b[15~'); pump_until(lambda:b'Choose source' in output)
                send(b'\t\t\n'); pump_until(lambda:b'New name' in output)
                send(b'\x1bOHcopy-\n'); pump_until(lambda:b'copy-source' in output)
                send(b'\t\t\n')
            else: send(b'\x1b[19~\t\n')
            pump_until(lambda:b'G\n' in events and b'Completed items:' in output)
            assert b'Completed items:' in output and b'Cancel stops here' in output
            if copy: assert b'Copied bytes:' in output
            # None of these keys may be dispatched as main-window commands.
            payload=b'qqqcn\x1bOP\x1b[18~\x1b[20~'  # quit, copy, new, help, Options, Menu
            if action=='esc': payload+=b'\x1b'
            elif action=='mouse':
                w=min(76,width-4); x=(width-w)//2+4; y=(height-7)//2+5
                payload+=f'\x1b[<0;{x+1};{y+1}M'.encode()
            else:
                fcntl.ioctl(master,termios.TIOCSWINSZ,struct.pack('HHHH',10,52,0,0))
                os.kill(proc.pid,signal.SIGWINCH)
            payload+=b'qqq\x1bOP\x1b[19~'  # queued commands after Cancel must be discarded
            send(payload); os.write(command_w,b'x')
            pump_until(lambda:b'R cancelled ' in events)
            line=bytes(events).split(b'R ')[1].splitlines()[0].split()
            assert line[0]==b'cancelled' and line[3]==b'1',line
            assert line[1:3]==([b'0',b'65536'] if copy else [b'2',b'0']),line
            pump_until(lambda:(b'Copy cancelled' if copy else b'Delete cancelled') in output)
            assert proc.poll() is None
            if copy:
                assert source.stat().st_size==1048576 and target.stat().st_size==65536
                send(b'\x1b')  # close retained copy form, if any
            else: assert len(list(source.iterdir()))==6
            # End explicitly after proving that the modal consumed queued Quit.
            send(b'q'); assert proc.wait(timeout=4)==0
        finally:
            if proc.poll() is None: proc.kill(); proc.wait()
            for fd in (master,event_r,command_w): os.close(fd)
print('PASS: pipe-gated PTY Cancel/Esc/resize, 50x9 modal, exact partial results and blocked background input')
