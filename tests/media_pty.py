"""Sixel protocol/lifecycle checks in a PTY. Does NOT prove real pixel display."""
import fcntl
import json
import os
from pathlib import Path
import pty
import re
import select
import signal
import struct
import subprocess
import tempfile
import termios
import time
from display_pty import Screen
from media_tools import STUB

BINARY=os.path.abspath(os.environ.get('TFILE_BINARY','./tfile'))
class GraphicsScreen(Screen):
    def __init__(self,h,w):
        super().__init__(h,w); self.raw=b''; self.images=[]; self.image=None; self.clears=0; self.saved=(0,0)
    def csi(self,params,command):
        if command=='s': self.saved=(self.y,self.x); return
        if command=='u': self.y,self.x=self.saved; return
        if command=='J' and params=='2': self.image=None; self.clears+=1
        super().csi(params,command)
    def cells(self,data):
        super().feed(data.replace(b'\x1b7',b'\x1b[s').replace(b'\x1b8',b'\x1b[u'))
    def feed(self,data):
        self.raw+=data
        while self.raw:
            at=self.raw.find(b'\x1bP')
            if at<0:
                # Keep a trailing ESC or ESC7/8 split across reads intact.
                n=len(self.raw)-1 if self.raw.endswith(b'\x1b') else len(self.raw)
                self.cells(self.raw[:n]); self.raw=self.raw[n:]; return
            self.cells(self.raw[:at]); self.raw=self.raw[at:]
            end=self.raw.find(b'\x1b\\',2)
            if end<0: return
            frame=self.raw[:end+2]; self.raw=self.raw[end+2:]
            dims=re.match(rb'\x1bP[0-9;]*q"1;1;(\d+);(\d+)',frame)
            assert dims,frame[:100]
            width,height=map(int,dims.groups())
            split=self.width*3//5 if self.width<80 else self.width*11//20
            assert self.x>=split+2 and self.y>=7,(self.x,self.y)
            assert self.x+(width+7)//8<=self.width-2
            assert self.y+(height+15)//16<=self.height-4
            self.image=(self.x,self.y,width,height);self.images.append(self.image)

class Terminal:
    def __init__(self,root,tools,log,w=100,h=24):
        self.master,slave=pty.openpty();self.screen=GraphicsScreen(h,w); self.output=b''
        fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',h,w,w*8,h*16))
        env={**os.environ,'PATH':str(tools),'LC_ALL':'C.UTF-8','TERM':'xterm-256color',
             'TFILE_SIXEL':'1','TFILE_CELL_PIXELS':'8x16','TFILE_MEDIA_TEST_LOG':str(log)}
        self.proc=subprocess.Popen([BINARY,str(root)],stdin=slave,stdout=slave,stderr=slave,env=env)
        os.close(slave);self.log=log
    def read(self,seconds=.15):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            if select.select([self.master],[],[],.01)[0]:
                try: data=os.read(self.master,65536)
                except OSError: break
                self.output+=data; self.screen.feed(data)
    def send(self,key): os.write(self.master,key.encode());self.read()
    def wait(self,predicate,seconds=2):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            self.read(.04)
            if predicate(): return
        raise AssertionError(('wait timed out',self.output[-500:]))
    def records(self): return [json.loads(x) for x in self.log.read_text().splitlines()] if self.log.exists() else []
    def text(self): return '\n'.join(self.screen.row(y) for y in range(self.screen.height))
    def resize(self,w,h):
        fcntl.ioctl(self.master,termios.TIOCSWINSZ,struct.pack('HHHH',h,w,w*8,h*16))
        self.screen.width=w;self.screen.height=h
        self.screen.rows=[[' ']*w for _ in range(h)]
        self.screen.scroll_bottom=h-1;self.screen.y=self.screen.x=0
        os.kill(self.proc.pid,signal.SIGWINCH);self.read()
    def close(self):
        try:
            if self.proc.poll() is None:
                self.send('q'); result=self.proc.wait(timeout=2)
                assert result==0,(result,self.output[-2000:].decode(errors='replace'))
        finally:
            if self.proc.poll() is None: self.proc.kill();self.proc.wait()
            if self.master>=0:os.close(self.master);self.master=-1

with tempfile.TemporaryDirectory(prefix='tfile-media-pty-') as directory:
    base=Path(directory); root=base/'files';root.mkdir(); tools=base/'tools';tools.mkdir();log=base/'log'
    for tool in ['magick','pdftoppm','pdftotext']:
        (tools/tool).write_text(STUB);(tools/tool).chmod(0o700)
    (root/'a-slow.png').write_bytes(b'\x89PNG\r\n\x1a\nslow')
    (root/"b 한글 'quote\" [0].jpg").write_bytes(b'\xff\xd8\xffok')
    (root/'c-page.pdf').write_bytes(b'%PDF-1.4\nok')
    (root/'d-text.txt').write_text('text body\n'*80)
    (root/'e-broken.png').write_bytes(b'\x89PNG\r\n\x1a\nfail')
    (root/'f-zero.png').touch()
    (root/'g-fake.png').write_text('ordinary text with image suffix')
    before={x.name for x in Path('/tmp').glob('tfile-media-*')}
    t=Terminal(root,tools,log)
    try:
        t.wait(lambda:len(t.records())>=1)
        assert 'Loading preview' in t.text()
        slow_pid=t.records()[0]['pid']
        start=time.monotonic(); t.send('\x1bOB')
        t.wait(lambda:t.screen.image is not None)
        assert time.monotonic()-start<1.5
        assert not Path(f'/proc/{slow_pid}').exists()
        assert '한글' in t.text() and 'Modified:' in t.text()
        conversions=len(t.records()); initial=t.screen.image
        t.send('\t');t.send('\t');assert len(t.records())==conversions
        t.send('r');t.wait(lambda:len(t.records())>conversions and t.screen.image is not None)
        assert len(t.records())==conversions+1;conversions+=1
        t.send('\x1bOP');assert t.screen.image is None and t.screen.clears>0
        t.send('\x1b');assert t.screen.image==initial and len(t.records())==conversions
        t.resize(80,24);t.wait(lambda:len(t.records())>conversions and t.screen.image is not None)
        assert len(t.records())==conversions+1
        # Tiny screens clear graphics without conversion; resizing restores them.
        t.resize(50,9);assert t.screen.image is None
        conversions=len(t.records());t.send('\t');t.send('\t');assert len(t.records())==conversions
        t.resize(100,24);t.wait(lambda:t.screen.image is not None)
        # First PDF page uses exactly one renderer followed by one image conversion.
        t.send('\x1bOB');t.wait(lambda:any(x['tool']=='pdftoppm' for x in t.records()) and t.screen.image is not None)
        assert sum(x['tool']=='pdftoppm' for x in t.records())==1
        # Image setting Off cancels/hides pixels while PDF text remains readable.
        t.send('\x1b[18~'+'\x1bOB'*5+'\n'+'\x1b')
        t.wait(lambda:'First page text' in t.text())
        assert t.screen.image is None
        t.send('\x1b[18~'+'\x1bOB'*5+'\n'+'\x1b');t.wait(lambda:t.screen.image is not None)
        # Files-only mode stops image work, then Enter enables preview again.
        t.send('\x1b[18~'+'\x1bOB'+'\n'+'\x1b'); assert t.screen.image is None
        t.send('\n');t.wait(lambda:t.screen.image is not None)
        t.send('\x1b');t.send('\x1bOB')
        assert t.screen.image is None and 'text body' in t.text()
        t.send('\n');t.send('\x1b[6~'); assert 'Row ' in t.text();t.send('\x1b');t.send('\x1bOB')
        t.wait(lambda:'Cannot display preview' in t.text())
        assert t.screen.image is None and b'\x1b[2J hostile' not in t.output
        t.send('\x1bOB');assert 'Empty file' in t.text()
        t.send('\x1bOB');assert 'ordinary text' in t.text()
        # Exit while converting: child and private artifacts are collected.
        conversions=len(t.records());t.send('\x1bOH');t.wait(lambda:len(t.records())>conversions)
        pid=t.records()[-1]['pid'];t.close();t=None
        assert not Path(f'/proc/{pid}').exists()
        assert {x.name for x in Path('/tmp').glob('tfile-media-*')}==before
    finally:
        if t is not None:t.close()
    # Wall-clock deadline is enforced without a keypress, while input stays live.
    log.write_text('')
    t=Terminal(root,tools,log)
    try:
        t.wait(lambda:len(t.records())>=1)
        t.send('\t');t.send('\t')
        t.wait(lambda:'timed out' in t.text(),seconds=10)
        assert t.screen.image is None
        pid=t.records()[0]['pid'];t.wait(lambda:not Path(f'/proc/{pid}').exists())
    finally:t.close()
    # SIGTERM during conversion follows the same cleanup path as q.
    log.write_text('')
    t=Terminal(root,tools,log)
    try:
        t.wait(lambda:len(t.records())>=1)
        pid=t.records()[-1]['pid'];os.kill(t.proc.pid,signal.SIGTERM)
        t.wait(lambda:t.proc.poll() is not None)
        assert t.proc.returncode==0 and not Path(f'/proc/{pid}').exists()
        assert {x.name for x in Path('/tmp').glob('tfile-media-*')}==before
    finally:t.close()
    # Signal shutdown also unwinds a modal rather than leaving a dead input loop.
    log.write_text('')
    t=Terminal(root,tools,log)
    try:
        t.wait(lambda:len(t.records())>=1);t.send('\x1bOB');t.wait(lambda:t.screen.image is not None)
        t.send('\x1bOP');assert t.screen.image is None
        os.kill(t.proc.pid,signal.SIGTERM);t.wait(lambda:t.proc.poll() is not None)
        assert t.proc.returncode==0
    finally:t.close()
    # Missing converters do not prevent keyboard navigation or application exit.
    empty=base/'empty-tools';empty.mkdir();only=base/'only';only.mkdir()
    (only/'a.png').write_bytes(b'\x89PNG\r\n\x1a\nok')
    t=Terminal(only,empty,log)
    try:
        t.wait(lambda:'Missing ImageMagick' in t.text());assert t.screen.image is None
        t.send('\x1b[18~');t.send('\x1b');t.close();t=None
    finally:
        if t is not None:t.close()
    # Missing PDF image tools -> bounded pdftotext first-page fallback.
    (empty/'pdftotext').write_text(STUB);(empty/'pdftotext').chmod(0o700)
    (only/'a.png').unlink();(only/'a.pdf').write_bytes(b'%PDF-1.4\nok')
    t=Terminal(only,empty,log)
    try:
        t.wait(lambda:'First page text' in t.text())
        assert 'Missing pdftoppm' in t.text() and t.screen.image is None
    finally:t.close()
print('PASS: PTY protocol ordering/bounds/clear/modal/resize, responsive selection/exit cancellation, focus cache, Auto/Off/files-only, PDF-first-page/text fallback, safe errors, unchanged text scrolling, zero/fake image, missing tools; real graphics remain unverified')
