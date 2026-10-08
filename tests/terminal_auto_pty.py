"""Lazy Auto detection in real app PTYs; no claims about physical pixels."""
import fcntl
import os
from pathlib import Path
import pty
import signal
import struct
import subprocess
import sys
import tempfile
import termios
from media_pty import GraphicsScreen, Terminal, BINARY
from media_tools import STUB

DA=b'\x1b[?62;4c'; CELLS=b'\x1b[6;16;8t'
F7='\x1b[18~'; DOWN='\x1bOB'

class AutoScreen(GraphicsScreen):
    def csi(self,params,command):
        if command=='c': return
        super().csi(params,command)

class AutoTerminal(Terminal):
    def __init__(self,base,pixels=False):
        self.master,slave=pty.openpty();self.screen=AutoScreen(24,100);self.output=b'';self.log=base/'log'
        fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',24,100,800 if pixels else 0,384 if pixels else 0))
        env={**os.environ,'HOME':str(base),'XDG_CONFIG_HOME':str(base/'config'),
             'PATH':str(base/'bin'),'LC_ALL':'C.UTF-8','TERM':'xterm-256color','TFILE_MEDIA_TEST_LOG':str(self.log)}
        env.pop('TFILE_SIXEL',None);env.pop('TFILE_CELL_PIXELS',None)
        self.proc=subprocess.Popen([BINARY,str(base/'files')],stdin=slave,stdout=slave,stderr=slave,env=env)
        os.close(slave);self.read(.08)
        # cbreak/input_init must be ready before sending control keys. The first
        # painted frame provides a readiness boundary instead of a fixed sleep.
        self.wait(lambda:'Shown:' in self.text())
    def send(self,key):
        os.write(self.master,key.encode() if isinstance(key,str) else key);self.read(.08)
    def queries(self): return self.output.count(b'\x1b[c'),self.output.count(b'\x1b[16t')
    def reply(self,data=DA+CELLS): self.send(data)
    def resize_cells(self,w,h,cw=0,ch=0):
        fcntl.ioctl(self.master,termios.TIOCSWINSZ,struct.pack('HHHH',h,w,w*cw,h*ch))
        self.screen.width=w;self.screen.height=h;self.screen.rows=[[' ']*w for _ in range(h)]
        self.screen.scroll_bottom=h-1;self.screen.x=self.screen.y=0
        os.kill(self.proc.pid,signal.SIGWINCH);self.read(.08)
    def idle(self):
        self.read(.2)
        before=(len(self.output),len(self.screen.images),self.screen.clears)
        stat=Path(f'/proc/{self.proc.pid}/stat')
        def ticks():
            fields=stat.read_text().split(') ',1)[1].split();return int(fields[11])+int(fields[12])
        start=ticks();self.read(1.1)
        assert before==(len(self.output),len(self.screen.images),self.screen.clears),'idle redraw'
        assert ticks()-start<=2,'idle CPU polling'
    def close(self):
        if sys.exc_info()[0] is not None:
            self.proc.kill();self.proc.wait();os.close(self.master);return
        super().close()

def fixture(base,tools=True,off=False):
    root=base/'files';root.mkdir();(base/'bin').mkdir()
    (root/'a.png').write_bytes(b'\x89PNG\r\n\x1a\nok')
    (root/'b.jpg').write_bytes(b'\xff\xd8\xffok')
    (root/'c.pdf').write_bytes(b'%PDF-1.4\nok')
    (root/'z.txt').write_text('ordinary text')
    if tools:
        for name in ['magick','pdftoppm','pdftotext']:
            p=base/'bin'/name;p.write_text(STUB);p.chmod(0o755)
    if off:
        config=base/'config/tfile';config.mkdir(parents=True)
        (config/'settings.conf').write_text('version=1\nimage=0\n')
    return root

with tempfile.TemporaryDirectory(prefix='tfile-auto-lazy-') as d:
    base=Path(d);root=fixture(base)
    for name in ['a.png','b.jpg','c.pdf']: (root/name).unlink()
    t=AutoTerminal(base)
    try:
        t.idle();assert t.queries()==(0,0)
        (root/'a.png').write_bytes(b'\x89PNG\r\n\x1a\nok');t.send('r')
        assert t.queries()==(0,0)
        t.send('\x06a.png\n');t.wait(lambda:t.queries()==(1,1))
        t.reply();t.wait(lambda:t.screen.image is not None)
    finally:t.close()

with tempfile.TemporaryDirectory(prefix='tfile-auto-success-') as d:
    base=Path(d);root=fixture(base);t=AutoTerminal(base)
    try:
        t.wait(lambda:t.queries()==(1,1));assert not t.screen.images and not t.records()
        t.reply();t.wait(lambda:t.screen.image is not None)
        assert 'PNG:' in ' '.join(t.records()[0]['args'])
        t.idle();t.send(DOWN);t.wait(lambda:any('JPEG:' in ' '.join(e['args']) for e in t.records()))
        t.send(DOWN);t.wait(lambda:any(e['tool']=='pdftoppm' for e in t.records()) and t.screen.image is not None)
        assert t.queries()==(1,1) and not (base/'config/tfile/settings.conf').exists()
        t.send('\x1b[999~');t.send('\x1bOP');assert 'Help' in t.text();t.send('\x1b')
        t.resize_cells(80,20);assert t.queries()==(1,2) and t.screen.image is None
        t.reply(b'\x1b[6;20;10t');t.wait(lambda:t.screen.image is not None)
        assert any('320x120' in ' '.join(e['args']) for e in t.records()),t.records()
        t.idle()
        t.send(F7+DOWN*7);assert 'Enabled' in t.text() and t.screen.image is None
        t.send('\x1bOA'*2+'\n\x1b');t.idle();assert t.screen.image is None
        assert t.queries()==(1,2)
    finally:t.close()
    # No support or cell values are inherited from the preceding process.
    t=AutoTerminal(base)
    try:
        t.wait(lambda:t.queries()==(1,1));t.wait(lambda:'No terminal reply' in t.text())
        assert not t.screen.images;t.idle()
    finally:t.close()

with tempfile.TemporaryDirectory(prefix='tfile-auto-modal-resize-') as d:
    base=Path(d);fixture(base);t=AutoTerminal(base)
    try:
        t.reply();t.wait(lambda:t.screen.image is not None)
        t.resize_cells(160,40);t.reply(CELLS);t.wait(lambda:t.screen.image is not None)
        # Both geometries fit this tiny original raster: comparing raster limits
        # alone cannot tell that cached cell measurements are stale.
        t.send('\x1bOP');before=len(t.screen.images);t.resize_cells(180,42)
        assert len(t.screen.images)==before and t.screen.image is None
        assert t.queries()==(1,3)
        t.reply(CELLS);t.wait(lambda:t.screen.image is not None);t.idle()
    finally:t.close()

with tempfile.TemporaryDirectory(prefix='tfile-auto-ioctl-') as d:
    base=Path(d);fixture(base);t=AutoTerminal(base,pixels=True)
    try:
        t.reply(DA);t.wait(lambda:t.screen.image is not None)
        conversions=len(t.records())
        t.resize_cells(80,20,9,18);t.wait(lambda:t.screen.image is not None)
        assert t.queries()==(1,1)
        assert len(t.records())==conversions  # unscaled original survives new cell pixels
        t.idle()
    finally:t.close()

for name,response,hint in [
    ('no-response',b'','No terminal reply'),
    ('unsupported',b'\x1b[?62;1;2c'+CELLS,'Terminal has no Sixel'),
    ('malformed',b'\x1b[?62;qq4c\x1b[6;0;999t','No terminal reply'),
    ('no-cells',DA,'Cell pixels unknown'),
    ('bad-cells',DA+b'\x1b[6;999999;8t','Cell pixels unknown')]:
    with tempfile.TemporaryDirectory(prefix='tfile-auto-'+name+'-') as d:
        base=Path(d);root=fixture(base);t=AutoTerminal(base)
        try:
            if response:t.reply(response)
            t.wait(lambda:hint in t.text());assert not t.screen.images and not t.records()
            t.idle();t.reply();t.idle();assert not t.screen.images and t.queries()==(1,1)
            t.send(DOWN);assert hint in t.text();t.send(DOWN)
            t.wait(lambda:'PDF page 1 (text' in t.text())
            assert any(e['tool']=='pdftotext' for e in t.records()) and not t.screen.images
            t.send('\x1b[999~'+F7);assert 'Options' in t.text();t.send('\x1b');t.idle()
            assert len(list(root.iterdir()))==4 and t.queries()==(1,1)
        finally:t.close()

with tempfile.TemporaryDirectory(prefix='tfile-auto-off-') as d:
    base=Path(d);fixture(base,off=True)
    for _ in range(2):
        prior=len((base/'log').read_text().splitlines()) if (base/'log').exists() else 0
        t=AutoTerminal(base)
        try:
            t.idle();assert t.queries()==(0,0) and not t.screen.images and len(t.records())==prior
            t.send(DOWN*2);t.wait(lambda:'PDF page 1 (text' in t.text())
            assert t.queries()==(0,0) and not t.screen.images
        finally:t.close()
    t=AutoTerminal(base)
    try:
        t.send(F7+DOWN*5+'\n');assert t.queries()==(0,0)
        t.send('\x1b');t.wait(lambda:t.queries()==(1,1));t.reply();t.wait(lambda:t.screen.image is not None)
        assert 'image=0' in (base/'config/tfile/settings.conf').read_text()
    finally:t.close()

with tempfile.TemporaryDirectory(prefix='tfile-auto-missing-') as d:
    base=Path(d);fixture(base,tools=False);t=AutoTerminal(base)
    try:
        t.reply();t.wait(lambda:'Missing ImageMagick' in t.text());assert not t.screen.images
        assert 'No terminal reply' not in t.text();t.send(DOWN*2)
        t.wait(lambda:'Missing pdftotext (Poppler)' in t.text());t.idle()
    finally:t.close()

    t=AutoTerminal(base)
    try:
        t.wait(lambda:'No terminal reply' in t.text())
        assert 'Missing ImageMagick' in t.text() and not t.screen.images;t.idle()
    finally:t.close()

with tempfile.TemporaryDirectory(prefix='tfile-auto-switch-') as d:
    base=Path(d);root=fixture(base);(root/'a.png').write_bytes(b'\x89PNG\r\n\x1a\nslow')
    t=AutoTerminal(base)
    try:
        # Selection changes before the response; only the latest JPEG is converted.
        t.send('\x1b[<0;4;7M');t.reply(b'\x9b?62;4c\x9b6;16;8t')
        t.wait(lambda:t.screen.image is not None)
        assert all('PNG:' not in ' '.join(e['args']) for e in t.records()),t.records()
        assert any('JPEG:' in ' '.join(e['args']) for e in t.records())
        # Quick find owns text while detection/preview work still uses bounded input waits.
        t.send('\x06z.txt\n');assert 'ordinary text' in t.text() and t.screen.image is None
        t.send('\x1bOH');t.wait(lambda:any(e['mode']=='slow' for e in t.records()))
        pid=t.records()[-1]['pid'];t.send('\x1bOP');assert 'Help' in t.text()
        t.wait(lambda:not Path(f'/proc/{pid}').exists())
        assert t.screen.image is None
        t.send('\x1b');t.wait(lambda:len([e for e in t.records() if e['mode']=='slow'])==2)
        pid=t.records()[-1]['pid'];t.resize_cells(49,8)
        t.wait(lambda:not Path(f'/proc/{pid}').exists());t.idle()
        assert t.screen.image is None
        (root/'a.png').write_bytes(b'\x89PNG\r\n\x1a\nok');t.resize_cells(100,24)
        t.reply(CELLS);t.wait(lambda:t.screen.image is not None);t.idle()
    finally:t.close()
    assert not Path(f'/proc/{pid}').exists()

with tempfile.TemporaryDirectory(prefix='tfile-auto-input-') as d:
    base=Path(d);root=fixture(base);t=AutoTerminal(base)
    try:
        t.send(b'\x1b[?62;')  # Incomplete report; next framed F2 recovers without cancelling it.
        t.send('\x1bOQ');assert 'New - File or Directory' in t.text(),t.text()
        t.reply(DA+CELLS);t.send('한🙂q-name\n')
        assert (root/'한🙂q-name').exists() and len(list(root.iterdir()))==5
        assert not t.screen.images;t.idle()
        # Incomplete UTF-8 no longer forces permanent 25 ms idle redraws.
        t.send(b'\xed');t.idle();t.send(b'\x95\x9c');t.send(F7)
        assert 'Options' in t.text();t.send('\x1b');t.idle()
    finally:t.close()

with tempfile.TemporaryDirectory(prefix='tfile-auto-find-') as d:
    base=Path(d);fixture(base);t=AutoTerminal(base)
    try:
        t.send('\x06')
        # draw() flushes the preview before quick_find overlays its status row;
        # inspect the completed interactive frame, not that intermediate flush.
        t.wait(lambda:'No terminal reply' in t.text() and 'Find:' in t.text())
        t.reply();t.send('z.txt\n')
        assert 'ordinary text' in t.text() and not t.screen.images;t.idle()
    finally:t.close()

for action in ['resize','exit','signal']:
    with tempfile.TemporaryDirectory(prefix='tfile-auto-cancel-'+action+'-') as d:
        base=Path(d);root=fixture(base);t=AutoTerminal(base)
        try:
            if action=='resize':
                t.resize_cells(49,8);t.reply();t.idle();t.resize_cells(100,24)
                assert not t.screen.images and t.queries()==(1,1);t.idle()
            elif action=='exit':
                t.send('q');assert t.proc.wait(timeout=2)==0
            else:
                os.kill(t.proc.pid,signal.SIGTERM);t.read(.2);assert t.proc.wait(timeout=2)==0
            assert not t.records()
        finally:t.close()

with tempfile.TemporaryDirectory(prefix='tfile-auto-delayed-start-') as d:
    base=Path(d);fixture(base);actual=BINARY;launcher=base/'delayed-tfile'
    launcher.write_text('#!'+sys.executable+'\nimport os,sys,time\ntime.sleep(.25)\n'
                        +'os.execv('+repr(actual)+',['+repr(actual)+',*sys.argv[1:]])\n')
    launcher.chmod(0o700);BINARY=str(launcher);t=None
    try:
        t=AutoTerminal(base);t.send('\x06')
        t.wait(lambda:'No terminal reply' in t.text() and 'Find:' in t.text())
        t.reply();t.send('z.txt\n')
        assert 'ordinary text' in t.text() and not t.screen.images,t.text()
    finally:
        if t is not None:t.close()
        BINARY=actual

print('PASS: lazy Auto PNG/JPEG/PDF, session reuse/restart/resize, ioctl, Off, failed/late/malformed replies, missing tools, selection/modal/child cleanup, UTF-8/mouse/keys, delayed startup and idle waits')
