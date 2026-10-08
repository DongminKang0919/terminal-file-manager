"""Real decoder/renderer output dimensions; no graphical terminal is inspected."""
from pathlib import Path
import shutil
import struct
import tempfile
import zlib
from media_pty import Terminal


def png(path,width,height):
    def chunk(kind,data):
        return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    # Solid RGB avoids an external fixture or drawing-library dependency.
    raw=(b'\0'+b'\x30\x90\xd0'*width)*height
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))


def pdf(path):
    stream=b'0.2 0.6 0.8 rg 0 0 612 792 re f\n'
    objects=[b'<< /Type /Catalog /Pages 2 0 R >>',b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
             b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << >> /Contents 4 0 R >>',
             f'<< /Length {len(stream)} >>\nstream\n'.encode()+stream+b'endstream']
    data=b'%PDF-1.4\n';offsets=[]
    for i,obj in enumerate(objects,1):
        offsets.append(len(data));data+=f'{i} 0 obj\n'.encode()+obj+b'\nendobj\n'
    start=len(data);data+=b'xref\n0 5\n0000000000 65535 f \n'
    data+=b''.join(f'{offset:010} 00000 n \n'.encode() for offset in offsets)
    data+=f'trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n{start}\n%%EOF\n'.encode();path.write_bytes(data)

if not shutil.which('convert') or not shutil.which('pdftoppm'):
    print('SKIP: real ImageMagick/Poppler tools unavailable')
else:
    with tempfile.TemporaryDirectory(prefix='tfile-real-fit-') as directory:
        root=Path(directory)
        shapes=[(1200,300),(300,1200),(900,900),(12,8),(2400,1600)]
        for i,(w,h) in enumerate(shapes):png(root/f'{i}.png',w,h)
        pdf(root/'5.pdf')
        for screen in [(80,24),(260,80)]:
            t=Terminal(root,Path('/usr/bin'),root/'unused-log',*screen)
            try:
                for i,(ow,oh) in enumerate(shapes+[(612,792)]):
                    if i:t.send('\x1bOB')
                    t.wait(lambda:t.screen.image is not None,seconds=5)
                    x,y,w,h=t.screen.image
                    cols=t.screen.width-(t.screen.width*11//20)-4;rows=t.screen.height-14
                    mw=min(cols*8,1536);mh=min(rows*16,1152)//6*6
                    assert x==t.screen.width*11//20+2+(cols*8-w)//16
                    assert y==9+(rows*16-((h+5)//6*6))//32
                    assert w<=mw and h<=mh,(w,h,mw,mh)
                    assert abs(w/ow-h/oh)*min(ow,oh)<=2,(w,h,ow,oh)
                    if i<5:
                        scale=min(1,mw/ow,mh/oh)
                        assert abs(w-ow*scale)<=2 and abs(h-oh*scale)<=2,(w,h,scale)
                    else:
                        assert abs(min(mw/w,mh/h)-1)<.03,(w,h,mw,mh)
            finally:t.close()
    print('PASS: real ImageMagick/Poppler portrait/landscape/square/tiny/large/PDF output fits, preserves aspect and centers at 80x24/260x80; no visual terminal claim')
