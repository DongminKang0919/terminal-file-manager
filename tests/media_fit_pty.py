"""Encoded dimensions/placement oracle; does not claim visual Sixel inspection."""
import os
from pathlib import Path
import re
import signal
import tempfile
import time
from media_pty import Terminal
from media_tools import STUB


def check_image(t, original=None):
    x,y,width,height=t.screen.image
    w,h=t.screen.width,t.screen.height
    split=w*3//5 if w<80 else w*11//20
    columns,rows=w-split-4,h-14
    assert x==split+2+(columns*8-width)//16,(x,width,columns)
    assert y==9+(rows*16-((height+5)//6*6))//32,(y,height,rows)
    assert x+(width+7)//8<=w-2
    assert y+(height+15)//16<=h-5
    assert width<=1536 and height<=1152
    if original:
        ow,oh=original
        scale=min(1,min(columns*8,1536)/ow,(min(rows*16,1152)//6*6)/oh)
        assert (width,height)==(max(1,int(ow*scale)),max(1,int(oh*scale)))
    assert 'Tab: Preview' not in t.screen.row(h-1)
    assert t.screen.rows[2][2]=='*'

with tempfile.TemporaryDirectory(prefix='tfile-fit-') as directory:
    base=Path(directory);root=base/'files';root.mkdir();tools=base/'tools';tools.mkdir();log=base/'log'
    for tool in ['magick','pdftoppm','pdftotext']:
        (tools/tool).write_text(STUB);(tools/tool).chmod(0o700)
    shapes=[(1200,300),(300,1200),(900,900),(12,8),(2400,1600)]
    for i,(w,h) in enumerate(shapes):
        (root/f'{i}.png').write_bytes(b'\x89PNG\r\n\x1a\n'+f'shape:{w}x{h}'.encode())
    (root/'5.pdf').write_bytes(b'%PDF-1.4\nok')
    t=Terminal(root,tools,log)
    try:
        for i,shape in enumerate(shapes):
            if i: t.send('\x1bOB')
            t.wait(lambda:t.screen.image is not None)
            check_image(t,shape)
        t.send('\x1bOB');t.wait(lambda:t.screen.image is not None)
        check_image(t)
        t.send('\x1bOH');t.wait(lambda:t.screen.image is not None)
        # Repeated resize signals arriving within 120 ms launch one final job.
        before=len(t.records())
        for w,h in [(120,30),(140,35),(180,40),(260,80)]:
            import fcntl,struct,termios
            fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',h,w,w*8,h*16))
            t.screen.width=w;t.screen.height=h;t.screen.rows=[[' ']*w for _ in range(h)]
            t.screen.scroll_bottom=h-1;t.screen.y=t.screen.x=0
            os.kill(t.proc.pid,signal.SIGWINCH);t.read(.025)
        t.wait(lambda:len(t.records())>before and t.screen.image is not None and t.screen.image[2]>512)
        assert len(t.records())==before+1,t.records()
        check_image(t,shapes[0])
        # Tiny original: changes of position and available size reuse bytes.
        t.send('\x1bOB'*3);t.wait(lambda:t.screen.image is not None and t.screen.image[2]==12)
        before=len(t.records());t.resize(100,24);t.wait(lambda:t.screen.image is not None)
        assert len(t.records())==before
        check_image(t,shapes[3])
        t.send('\x1bOP');assert t.screen.image is None
        t.send('\x1b');t.wait(lambda:t.screen.image is not None);check_image(t,shapes[3])
        t.resize(49,8);assert t.screen.image is None
        t.resize(80,24);t.wait(lambda:t.screen.image is not None);check_image(t,shapes[3])
        t.send('\x1bOB');t.wait(lambda:t.screen.image is not None)
        t.resize(500,140);t.wait(lambda:t.screen.image is not None and t.screen.image[2]==1536)
        check_image(t,shapes[4]);before=len(t.records())
        t.resize(600,150);t.wait(lambda:t.screen.image is not None)
        assert len(t.records())==before  # saturated Fit limits: only placement changes
        check_image(t,shapes[4])
    finally:t.close()
print('PASS: portrait/landscape/square/tiny/large/PDF centering, bounds/aspect/no upscale, larger cap, resize coalescing, small-original cache, modal and minimum-screen clearing (protocol oracle)')
