"""Inspect search stage geometry and exercise deterministic cancellation with real results."""
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
from display_pty import Terminal, Screen

F3='\x1bOR'
for width,height in [(50,9),(80,24),(100,24),(160,32)]:
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory)
        (root/'a-first').write_text('first')
        (root/'b-한글').write_text('selected body')
        t=Terminal(directory,width,height)
        iw=min(60,width-8); ix=(width-iw)//2; iy=(height-7)//2
        rw=min(90,width-8); rx=(width-rw)//2; rh=height-2
        first=4 if height<12 else 5
        try:
            t.send('\x1bOB')
            before=[t.screen.row(y) for y in range(height)]
            t.send(F3)
            t.frame(ix,iy,iw,7)
            assert 'Enter: Open' not in '\n'.join(t.screen.row(y) for y in range(height))
            assert '[ Search ]  [ Cancel ]' in t.screen.row(iy+5)
            assert 'Enter: search' in t.screen.row(iy+4)
            t.click(1,first)  # background must not change selection
            t.send('한글')
            t.click(ix+12,iy+5)  # gap between buttons is inert
            t.frame(ix,iy,iw,7)
            t.click(ix+3,iy+5)
            t.frame(rx,1,rw,rh)
            assert 'Complete: 1 found' in t.screen.row(3)
            assert '한글' in t.screen.row(4)
            t.send('/')
            t.frame(ix,iy,iw,7)
            assert '한글' in t.screen.row(iy+3)
            t.send('\x1bOH\x1b[3~\x1b[3~a-first\t\n')
            t.frame(rx,1,rw,rh)
            assert 'a-first' in t.screen.row(4)
            t.click(rx+3,height-3)  # Search again from results
            t.frame(ix,iy,iw,7)
            assert 'a-first' in t.screen.row(iy+3)
            t.send('\x1bOH'+'\x1b[3~'*7+'no-match\n')
            t.frame(rx,1,rw,rh)
            assert 'Complete: 0 found' in t.screen.row(3)
            assert 'No results collected' in t.screen.row(4)
            t.send('/')
            assert 'no-match' in t.screen.row(iy+3)
            t.send('\t\t\n')  # keyboard Cancel
            assert [t.screen.row(y) for y in range(height)]==before
            t.send(F3); t.send('\x1b')  # input Esc
            assert [t.screen.row(y) for y in range(height)]==before
            t.send(F3); t.click(ix+15,iy+5)  # mouse Cancel
            assert [t.screen.row(y) for y in range(height)]==before
            t.send(F3); t.click(ix+iw-4,iy)  # input x
            assert [t.screen.row(y) for y in range(height)]==before
            t.send(F3+'a-first\n')
            t.click(rx+15,height-3)  # result Open button
            assert t.screen.rows[first][1]=='>'
            # Resize input and results: close, do not dispatch background input.
            for results in (False,True):
                t.send(F3+('a-first\n' if results else ''))
                t.screen=Screen(height,width+1)
                fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',height,width+1,0,0))
                os.kill(t.proc.pid,signal.SIGWINCH); t.read()
                assert 'Name contains:' not in '\n'.join(t.screen.row(y) for y in range(height))
                assert 'Enter: Open' in t.screen.row(height-1)
                assert t.screen.rows[first][1]=='>'
                t.screen=Screen(height,width)
                fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',height,width,0,0))
                os.kill(t.proc.pid,signal.SIGWINCH); t.read()
        finally:t.close()

class GatedTerminal(Terminal):
    def __init__(self, root,width,height):
        self.screen=Screen(height,width)
        self.master,slave=pty.openpty()
        fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',height,width,0,0))
        self.event_r,event_w=os.pipe(); command_r,self.command_w=os.pipe()
        self.proc=subprocess.Popen(['tests/tfile_search',root],stdin=slave,stdout=slave,stderr=slave,
            pass_fds=(event_w,command_r),env={**os.environ,'TERM':'xterm-256color','LC_ALL':'C.UTF-8',
            'TFILE_GATE_EVENT':str(event_w),'TFILE_GATE_COMMAND':str(command_r)})
        os.close(slave);os.close(event_w);os.close(command_r)
        self.events=b'';self.read()
    def event(self, marker):
        deadline=time.monotonic()+8
        while marker not in self.events:
            assert time.monotonic()<deadline,self.events
            for fd in select.select([self.master,self.event_r],[],[],.05)[0]:
                data=os.read(fd,65536); assert data
                if fd==self.master:self.screen.feed(data)
                else:self.events+=data
        self.read()
    def close(self):
        try:super().close()
        finally:os.close(self.event_r);os.close(self.command_w)

for width,height in [(50,9),(100,24)]:
    for action in ('esc','cancel','enter','x','resize'):
        with tempfile.TemporaryDirectory() as directory:
            for i in range(130):(Path(directory)/f'match-{i:03}').write_text('body')
            t=GatedTerminal(directory,width,height)
            rw=min(90,width-8);rx=(width-rw)//2
            try:
                if action in ('x','resize'):
                    t.send('\x1bOQmatch-created\n')
                    assert '[Success]' in t.screen.row(height-2)
                t.send(F3+'match\n');t.event(b'G\n')
                t.frame(rx,1,rw,height-2)
                assert 'Searching' in t.screen.row(3)
                assert 'Enter: open' not in t.screen.row(height-4)
                assert 'keep results' in t.screen.row(height-4)
                payload='q\x1bOQ\x1b[18~'  # blocked main Quit, New and Options
                if action=='esc':payload+='\x1b'
                elif action=='enter':payload+='\n'
                elif action=='cancel':payload+=f'\x1b[<0;{rx+4};{height-2}M'
                elif action=='x':payload+=f'\x1b[<0;{rx+rw-3};2M'
                else:
                    t.screen=Screen(10,52)
                    fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',10,52,0,0))
                    os.kill(t.proc.pid,signal.SIGWINCH)
                os.write(t.master,payload.encode());os.write(t.command_w,b'x');t.event(b'R ')
                assert b'R 1 64\n' in t.events,t.events
                if action in ('esc','cancel','enter'):
                    t.frame(rx,1,rw,height-2)
                    assert 'Cancelled: 64 found' in t.screen.row(3)
                    assert 'match-' in t.screen.row(4)
                    t.send('\n')  # retained results remain openable
                    assert 'Opened search result' in t.screen.row(height-2)
                else:
                    actual_h=10 if action=='resize' else height
                    assert 'Search cancelled: 64 found' in t.screen.row(actual_h-2)
                    assert 'Enter: Open' in t.screen.row(actual_h-1)
                    t.send('!');assert 'File operation: Success' in '\n'.join(t.screen.row(y) for y in range(actual_h));t.send('\x1b')
                assert len(list(Path(directory).iterdir()))==130+(action in ('x','resize'))
            finally:t.close()
print('PASS: search input/result geometry, unicode edit, focus flow, buttons/gaps, restore, resize; gated Cancel/Esc/Enter keeps 64 results, x/resize closes, background blocked')
