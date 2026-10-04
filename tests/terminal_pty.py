"""Protocol/UX/process tests. PTYs cannot confirm actual pixel rendering/erasure."""
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
import time
from media_pty import GraphicsScreen, Terminal, BINARY
from media_tools import STUB

F7='\x1b[18~'; DOWN='\x1bOB'
class SetupScreen(GraphicsScreen):
    def csi(self,params,command):
        if command=='c': return  # DA query, answered by this test's terminal double.
        super().csi(params,command)
    def bounds(self,width,height):
        if (width,height)==(24,12):
            w=min(70,self.width-4); h=min(18,self.height-2)
            assert self.x==(self.width-w)//2+2 and self.y==(self.height-h)//2+3
            assert self.x+3<=self.width-2 and self.y+1<=self.height-3
        else:
            super().bounds(width,height)
class SetupTerminal(Terminal):
    def __init__(self,root,tools,log,w=100,h=24,pixels=False,advanced=False):
        self.master,slave=pty.openpty(); self.screen=SetupScreen(h,w); self.output=b''; self.log=log
        fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',h,w,w*8 if pixels else 0,h*16 if pixels else 0))
        env={**os.environ,'PATH':str(tools),'LC_ALL':'C.UTF-8','TERM':'xterm-256color',
             'TFILE_MEDIA_TEST_LOG':str(log)}
        env.pop('TFILE_SIXEL',None); env.pop('TFILE_CELL_PIXELS',None)
        if advanced: env.update(TFILE_SIXEL='1',TFILE_CELL_PIXELS='8x16')
        self.proc=subprocess.Popen([BINARY,str(root)],stdin=slave,stdout=slave,stderr=slave,env=env)
        os.close(slave); self.read()
    def send(self,key):
        os.write(self.master,key.encode() if isinstance(key,str) else key);self.read()
    def setup(self):
        self.send(F7+DOWN*7+'\n'); self.wait(lambda:'Image tool:' in self.text() and ('pdftoppm: found' in self.text() or 'pdftoppm: missing' in self.text()) and ('pdftotext: found' in self.text() or 'pdftotext: missing' in self.text()))
    def query(self,response=b'\x1b[?62;4c\x1b[6;16;8t'):
        before=len(self.output); self.send('\n')
        assert b'\x1b[c\x1b[16t' in self.output[before:]
        if response: os.write(self.master,response)
        self.wait(lambda:(self.screen.image is not None and self.screen.image[2:]==(24,12)) or 'Auto check inconclusive' in self.text() or 'Cell size unknown;' in self.text(),2)
    def confirm(self):
        self.send('y'); assert 'Did the image disappear' in self.text() and self.screen.image is None
        self.send('y'); assert 'Enable image preview' in self.text()
        self.send('y'); assert 'enabled for this session' in self.text()
    def close(self):
        if sys.exc_info()[0] is not None:
            self.proc.kill();self.proc.wait();os.close(self.master);return
        super().close()
    def idle(self):
        self.read(.25)  # Let conversion and the last screen update settle.
        before=(len(self.output),len(self.screen.images),self.screen.clears)
        stat=Path(f'/proc/{self.proc.pid}/stat')
        def ticks():
            fields=stat.read_text().split(') ',1)[1].split()
            return int(fields[11])+int(fields[12])
        start=ticks(); self.read(1.2)
        assert before==(len(self.output),len(self.screen.images),self.screen.clears), 'idle redraw/output'
        assert ticks()-start<=2, 'idle CPU polling'
    def click_close(self):
        w=min(70,self.screen.width-4); h=min(18,self.screen.height-2)
        x=(self.screen.width-w)//2+w-4; y=(self.screen.height-h)//2
        self.send(f'\x1b[<0;{x+1};{y+1}M')
        assert 'Image check cancelled; previous settings kept' in self.text(),self.text()
        assert self.screen.image is None
        assert 'Options' in self.text()
    def close_setup(self): self.send('\x1b')

for width,height in [(50,9),(100,24)]:
    with tempfile.TemporaryDirectory(prefix='tfile-terminal-pty-') as directory:
        base=Path(directory); root=base/'files';root.mkdir();tools=base/'bin';tools.mkdir();log=base/'log'
        os.environ.update(HOME=directory,XDG_CONFIG_HOME=str(base/'config'))
        config=base/'config/tfile/settings.conf'
        for tool in ['magick','pdftoppm','pdftotext']:
            p=tools/tool;p.write_text(STUB);p.chmod(0o755)
        (root/'image').write_bytes(b'\x89PNG\r\n\x1a\nok')
        t=SetupTerminal(root,tools,log,width,height)
        try:
            assert 'set up in F7' in t.text() or width==50
            assert not t.screen.images and not log.exists()
            t.setup(); t.query()
            assert t.screen.images[-1][2:]==(24,12)
            t.confirm(); assert not config.exists()
            # F7 retains the diagnostic item focus. Rechecking does not commit candidates early.
            t.send('\n'); assert 'Enabled (8x16)' in t.text()
            t.query(b'\x1b[?62;4c\x1b[6;32;16t'); t.send('n')
            t.send('\n'); assert 'Enabled (8x16)' in t.text();t.send('\x1b')
            t.close_setup()
            if width>=80:
                t.wait(lambda:t.screen.image is not None)
                assert t.screen.image[2:]==(6,6) and t.records()
            t.idle()
            t.send(b'\x1b[999~');t.send(F7);assert 'Options' in t.text();t.send('\x1b');t.idle()
            t.setup(); t.query(b'\x9b?62;4c\x9b6;16;8t'); t.send('y');t.send('n')  # Failed erase confirmation.
            t.send('\n');assert 'Enabled (8x16)' in t.text();t.send('\x1b');t.close_setup()
            # Normal fields/Unicode/mouse still work after the raw response filter is armed.
            t.send('\x06image\n');t.send(F7);assert 'Options' in t.text();t.send('\x1b')
        finally:t.close()
        # A new process on a terminal with no pixel information cannot inherit the verification.
        t=SetupTerminal(root,tools,log,width,height)
        try:
            assert not t.screen.images
            t.setup();assert 'Unconfirmed' in t.text()
            # No reply does not assert "unsupported"; keys during query remain modal.
            t.send('\n');t.send('qqcn123');t.wait(lambda:'Auto check inconclusive' in t.text())
            assert t.proc.poll() is None and len(list(root.iterdir()))==1
            # Invalid replies, then valid fragmented reports within one diagnostic.
            t.send('\n');os.write(t.master,b'\x1b[?62;qq4c\x1b[6;0;999t')
            t.wait(lambda:'Auto check inconclusive' in t.text())
            t.send('\n');os.write(t.master,b'\x1b');time.sleep(.02);os.write(t.master,b'[?62;')
            time.sleep(.08);os.write(t.master,b'4c\x1b[6;16;');time.sleep(.08);os.write(t.master,b'8t')
            t.wait(lambda:'Is red/blue' in t.text());t.confirm();t.close_setup()
            # A complete late response after timeout and modal closure must never run shortcuts.
            t.setup();t.query(b'');t.send('\x1b');t.close_setup()
            t.send(b'\x1b[?62;4;qqF8c\x1b[6;16;8t\x9b?62;4c\x9b6;16;8t')
            assert t.proc.poll() is None and len(list(root.iterdir()))==1
            # A partial response survives cancellation and modal closure; its tail is quarantined.
            t.setup();t.send('\n');t.send(b'\x1b[?62;');t.send('\x1b')
            t.send(b'4c');t.close_setup();t.send(b'\x1b[6;16;8t')
            assert t.proc.poll() is None and len(list(root.iterdir()))==1
        finally:t.close()
print('PASS: explicit terminal setup, split/invalid/no/late replies, display/erase failures, recheck, F7 focus, Unicode, no implicit persistence and fresh-session isolation at 50x9/100x24')

with tempfile.TemporaryDirectory(prefix='tfile-terminal-extra-') as directory:
    base=Path(directory);root=base/'files';root.mkdir();tools=base/'bin';tools.mkdir();log=base/'log'
    os.environ.update(HOME=directory,XDG_CONFIG_HOME=str(base/'config'))
    (root/'image').write_bytes(b'\x89PNG\r\n\x1a\nok')
    # Terminal verification does not require any converter. Measured manual
    # dimensions undergo the same three independent confirmations.
    t=SetupTerminal(root,tools,log)
    try:
        t.setup(); assert 'missing; install ImageMagick' in t.text() and 'pdftoppm: missing' in t.text(),t.text()
        t.send('m');assert 'Measured WIDTHxHEIGHT' in t.text()
        t.send('10x999\n');t.wait(lambda:'Invalid measured size' in t.text())
        t.send('m');t.send('8x16\n');t.wait(lambda:'Is red/blue' in t.text());t.confirm();t.close_setup()
        t.wait(lambda:'Missing ImageMagick' in t.text())
        assert not t.records()
        t.setup();t.query(b'\x1b[?62;4c');assert 'Cell size unknown' in t.text()
        t.send('\x1b');t.close_setup()
    finally:t.close()
    for tool in ['magick','pdftoppm','pdftotext']:
        p=tools/tool;p.write_text(STUB);p.chmod(0o755)
    # Close from every confirmation and from the query loop; settings/focus survive.
    t=SetupTerminal(root,tools,log)
    try:
        for stage in range(4):
            t.setup()
            if stage:
                t.query()
                for _ in range(stage-1): t.send('y')
            t.click_close()
            t.send('\n');assert 'Unconfirmed' in t.text()
            t.click_close();t.close_setup()
        t.setup();t.send('\n');t.click_close()
        t.close_setup();t.idle()
        t.send(b'\x1b[?62;qqF8c\x1b[6;16;8t')
        assert t.proc.poll() is None
        t.send(F7);assert 'Options' in t.text();t.send('\x1b')
    finally:t.close()
    # ioctl cell geometry is useful, but cannot confirm Sixel by itself.
    t=SetupTerminal(root,tools,log,pixels=True)
    try:
        t.setup();t.query(b'\x1b[?62;4c');t.confirm();t.close_setup();t.wait(lambda:t.screen.image is not None)
        (root/'zjpeg').write_bytes(b'\xff\xd8\xffok')
        (root/'zzpdf').write_bytes(b'%PDF-1.4\nok')
        t.send('r'+DOWN);t.wait(lambda:t.screen.image is not None and any('JPEG:' in ' '.join(e['args']) for e in t.records()))
        t.send(DOWN);t.wait(lambda:t.screen.image is not None and any(e['tool']=='pdftoppm' for e in t.records()))
        t.send('\x1bOH')
        # Recheck while the old converter is running: retire first, no new
        # query/test output until its process/FD/temp resources are gone.
        (root/'image').write_bytes(b'\x89PNG\r\n\x1a\nslow')
        t.send('r');t.wait(lambda:any(e['mode']=='slow' for e in t.records()))
        pid=next(e['pid'] for e in reversed(t.records()) if e['mode']=='slow')
        environment=Path(f'/proc/{pid}/environ').read_bytes().split(b'\0')
        temporary=next(v.split(b'=',1)[1] for v in environment if v.startswith(b'MAGICK_TEMPORARY_PATH='))
        before={Path(os.fsdecode(temporary))}
        t.setup();t.query(); assert not Path(f'/proc/{pid}').exists()
        assert len(list(Path(f'/proc/{t.proc.pid}/fd').iterdir()))<=7
        assert not (set(Path('/tmp').glob('tfile-media-*')) & before)
        # Resize clears the test image and closes both stale popup geometries.
        n=len(t.screen.images);t.resize(80,20)
        assert 'Image display setup' not in t.text() and t.screen.image is None
        assert 'F7: retry image check' in t.text()
        assert len(t.screen.images)==n
        # Late valid reports after a resize still cannot act as file commands.
        t.send(b'\x1b[?62;4c\x1b[6;16;8t');assert t.proc.poll() is None
        (root/'image').write_bytes(b'\x89PNG\r\n\x1a\nok');t.send('r')
    finally:t.close()
    assert not Path(f'/proc/{pid}').exists()
    assert not (set(Path('/tmp').glob('tfile-media-*')) & before)
    # Existing environment values remain usable; a failed recheck keeps them.
    t=SetupTerminal(root,tools,log,pixels=True,advanced=True)
    try:
        t.wait(lambda:t.screen.image is not None);t.setup();assert 'Enabled (8x16)' in t.text()
        t.query(b'\x1b[?62;4c\x1b[6;32;16t');t.send('n')
        t.send('\n');assert 'Enabled (8x16)' in t.text();t.send('\x1b');t.close_setup()
    finally:t.close()
    t=SetupTerminal(root,tools,log)
    try:
        t.setup();t.send('\n');t.send(b'\x1b[6;');t.resize(80,20)
        assert 'Image display setup' not in t.text() and not t.screen.images
        t.send(b'16;8t\x1b[?62;4c');assert t.proc.poll() is None
        t.setup();assert 'Unconfirmed' in t.text();t.send('\x1b');t.close_setup()
    finally:t.close()
print('PASS: measured manual input, missing tools vs unknown cells, ioctl fallback, advanced environment preservation, active converter/FD/temp cleanup and resize abort')

with tempfile.TemporaryDirectory(prefix='tfile-terminal-workflow-') as directory:
    base=Path(directory);root=base/'files';root.mkdir();tools=base/'bin';tools.mkdir();log=base/'log'
    os.environ.update(HOME=directory,XDG_CONFIG_HOME=str(base/'config'))
    folder=root/'folder';folder.mkdir();(folder/'leaf.txt').write_text('after F7')
    destination=base/'copies';destination.mkdir()
    t=SetupTerminal(root,tools,log)
    try:
        t.setup();t.query();t.confirm();t.close_setup();t.idle()
        t.send('\x06folder\n\n');assert str(folder) in t.text()
        t.send('\x1bORleaf\n');t.send('\n');assert 'leaf.txt' in t.text()
        t.send('\x1b[15~'+str(destination)+'\n')
        assert not (destination/'leaf.txt').exists()
        for y in range(t.screen.height):
            row=t.screen.row(y)
            if '[ Copy now ]' in row:
                t.send(f'\x1b[<0;{row.index("[ Copy now ]")+3};{y+1}M');break
        else:raise AssertionError(t.text())
        assert (destination/'leaf.txt').read_text()=='after F7'
        assert (folder/'leaf.txt').read_text()=='after F7'
        t.send('\x1b[?62;qqF8c\x1b[6;16;8t');t.send(F7)
        assert 'Options' in t.text();t.send('\x1b');t.idle()
    finally:t.close()
print('PASS: F7 verification followed by navigation, search, explicit mouse copy and late reply isolation')
