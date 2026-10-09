"""Repeatable PTY traffic measurement; no real terminal latency/flicker claim.
Run with TFILE_BINARY pointing to either build; --verify checks retained frames.
"""
import argparse
import fcntl
import json
import os
from pathlib import Path
import re
import select
import signal
import struct
import tempfile
import termios
import time
from media_pty import GraphicsScreen, Terminal
from media_tools import STUB

class MeasuredTerminal(Terminal):
    def read(self,seconds=.15):
        # Bound select by the remaining interval. The shared test reader uses
        # fixed 10ms waits, which would inflate small-frame acknowledgement
        # times merely because parsing them finishes within a 1ms sample.
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            if select.select([self.master],[],[],min(.01,max(0,end-time.monotonic())))[0]:
                try: data=os.read(self.master,65536)
                except OSError: break
                self.output+=data; self.screen.feed(data)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--verify', action='store_true')
args = parser.parse_args()
frames = re.compile(rb'\x1bP[0-9;]*q.*?\x1b\\', re.S)
if args.verify:
    # The oracle must catch both a pixel write and EL beginning left of pixels.
    for edit in [b'\x1b[11;58HX', b'\x1b[11;2H\x1b[K', b'\x1b[2;1H\x1b[J']:
        oracle=GraphicsScreen(30,100)
        oracle.feed(b'\x1b[11;58H\x1bPq"1;1;8;6!8~\x1b\\')
        oracle.feed(b'\x1b[2;1HX')
        assert oracle.image is not None and not oracle.damage_events
        oracle.feed(edit)
        assert oracle.image is None and oracle.damage_events==1


with tempfile.TemporaryDirectory(prefix='tfile-redraw-') as directory:
    base=Path(directory); root=base/'files'; root.mkdir(); tools=base/'tools'; tools.mkdir(); log=base/'log'
    # Uncompressed, valid raster makes retransmitted byte counts visible.
    stub=STUB.replace("frame+=f'!{w}'+chr(63+(1<<min(6,h-row))-1)",
                      "frame+=chr(63+(1<<min(6,h-row))-1)*w")
    for name in ['magick','pdftoppm','pdftotext']:
        (tools/name).write_text(stub); (tools/name).chmod(0o700)
    (root/'a.png').write_bytes(b'\x89PNG\r\n\x1a\nshape:1200x300')
    (root/'b.jpg').write_bytes(b'\xff\xd8\xffshape:300x1200')
    (root/'c.pdf').write_bytes(b'%PDF-1.4\nok')
    (root/'d.txt').write_text('plain preview\n')
    results=[]
    t=MeasuredTerminal(root,tools,log,w=100,h=30)
    def measure(name, action, ready=None, idle=False):
        start=len(t.output); conversions=len(t.records()); clears=t.screen.clears
        begin=time.monotonic(); action()
        if ready:
            deadline=begin+3
            while not ready():
                assert time.monotonic()<deadline, name
                t.read(.001)
        latency=None if idle else round((time.monotonic()-begin)*1000,2)
        # Drain output after the visible cell/protocol acknowledgement.
        t.read(.1)
        data=t.output[start:]; sixels=frames.findall(data)
        row=dict(scenario=name,converter_processes=len(t.records())-conversions,
                 sixel_frames=len(sixels),sixel_bytes=sum(map(len,sixels)),
                 ed2=t.screen.clears-clears,pty_bytes=len(data),ack_ms=latency)
        results.append(row)
        if args.verify and name in ['idle','mark on','mark off','status only']:
            assert not row['converter_processes'] and not row['sixel_frames'] and not row['ed2'],row
            assert t.screen.image is not None,(name,'image erased by text output')
        return row
    def key(value): os.write(t.master,value.encode())
    try:
        t.wait(lambda:t.screen.image is not None)
        initial=t.screen.image
        measure('idle',lambda:t.read(.5),idle=True)
        measure('mark on',lambda:key(' '),lambda:'Marked: 1' in t.screen.row(28))
        measure('mark off',lambda:key(' '),lambda:'Marked: 0' in t.screen.row(28))
        # Empty forward history changes only status; no navigation or modal.
        measure('status only',lambda:key(']'),lambda:'Forward:' in t.text())
        measure('image to image',lambda:key('\x1bOB'),lambda:t.screen.image is not None and t.screen.image!=initial)
        measure('image to PDF',lambda:key('\x1bOB'),lambda:any(x['tool']=='pdftoppm' for x in t.records()) and t.screen.image is not None and 'c.pdf' in t.text())
        measure('PDF to text',lambda:key('\x1bOB'),lambda:'plain preview' in t.text() and t.screen.image is None)
        key('\x1bOH');t.wait(lambda:t.screen.image is not None)
        measure('modal open',lambda:key('\x1bOP'),lambda:'Help' in t.text() and t.screen.image is None)
        measure('modal close',lambda:key('\x1b'),lambda:t.screen.image is not None)
        def resize_burst():
            for w,h in [(120,32),(140,35),(180,40),(100,30)]:
                fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',h,w,w*8,h*16))
                t.screen.width=w;t.screen.height=h;t.screen.rows=[[' ']*w for _ in range(h)]
                t.screen.scroll_bottom=h-1;t.screen.x=t.screen.y=0
                os.kill(t.proc.pid,signal.SIGWINCH);t.read(.025)
        measure('resize burst',resize_burst,lambda:t.screen.image is not None and t.screen.width==100)
        if args.verify:
            assert all(r['ed2'] and r['sixel_frames'] for r in results if r['scenario'] in ['image to image','image to PDF','resize burst'])
            assert next(r for r in results if r['scenario']=='PDF to text')['ed2']>0
            assert next(r for r in results if r['scenario']=='modal close')['sixel_frames']>0
    finally:t.close()
    if args.verify:
        assert not t.screen.damage_events, t.screen.damage_events
        # PDF pixels have the same retention policy; verify stable marks and
        # routine guidance after the first-page renderer/encoder completes.
        renderers=sum(r['tool']=='pdftoppm' for r in t.records())
        t=MeasuredTerminal(root,tools,log,w=100,h=30)
        try:
            t.wait(lambda:t.screen.image is not None); t.send('\x1bOB'*2)
            t.wait(lambda:sum(r['tool']=='pdftoppm' for r in t.records())>renderers and
                   t.screen.image is not None and t.screen.image[2:]==(6,6))
            n=len(t.screen.images); c=t.screen.clears; jobs=len(t.records())
            t.send(' '); t.send(' '); t.send(']'); t.send('z')
            assert len(t.screen.images)==n and t.screen.clears==c and len(t.records())==jobs
            assert t.screen.image is not None and t.screen.rows[2][2]=='*'
            output=len(t.output); t.read(.3); assert len(t.output)==output
            assert not t.screen.damage_events
        finally:t.close()
        # A list edit on an image-occupied row cannot safely retain pixels:
        # even an EL issued left of the preview can erase across both panels.
        overlap=base/'overlap'; overlap.mkdir()
        for i in range(11): (overlap/f'a{i:02}.txt').write_text('text')
        (overlap/'z.png').write_bytes(b'\x89PNG\r\n\x1a\nshape:300x1200')
        t=MeasuredTerminal(overlap,tools,log,w=100,h=30)
        try:
            t.wait(lambda:'Shown:' in t.text()); t.send('\x1bOF')
            t.wait(lambda:t.screen.image is not None)
            ix,iy,iw,ih=t.screen.image
            assert iy<=16<iy+(ih+15)//16
            n=len(t.screen.images); c=t.screen.clears; jobs=len(t.records())
            t.send(' '); t.wait(lambda:'Marked: 1' in t.screen.row(28))
            assert len(t.screen.images)>n and t.screen.clears>c
            assert len(t.records())==jobs and t.screen.image is not None
            assert t.screen.rows[2][2]=='*'  # no implicit Preview focus
            n=len(t.screen.images); c=t.screen.clears
            t.send(']'); assert len(t.screen.images)==n and t.screen.clears==c
            t.send('r'); t.wait(lambda:len(t.records())>jobs and t.screen.image is not None)
            assert len(t.screen.images)>n  # explicit screen recovery cannot reuse old pixels
            assert not t.screen.damage_events, t.screen.damage_events
        finally:t.close()
    if args.verify:
        # An unmeasured terminfo profile retains the old conservative policy.
        t=MeasuredTerminal(root,tools,log,w=100,h=30,term='xterm')
        try:
            t.wait(lambda:t.screen.image is not None)
            n=len(t.screen.images); c=t.screen.clears; jobs=len(t.records())
            t.send(' ')
            assert len(t.screen.images)>n and t.screen.clears>c
            assert len(t.records())==jobs and t.screen.image is not None
            assert not t.screen.damage_events
        finally:t.close()
    print(json.dumps(dict(binary=os.environ.get('TFILE_BINARY','./tfile'),terminal='xterm-256color',
                         columns=100,rows=30,cell_pixels='8x16',results=results),indent=2))
