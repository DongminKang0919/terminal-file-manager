"""Optional real ImageMagick/Poppler conversion tests; no real terminal assertion."""
import hashlib
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

PROBE=os.path.abspath(os.environ.get('TFILE_MEDIA_TEST','./tests/media_test'))

def rc4(key,data):
    s=list(range(256));j=0
    for i in range(256):
        j=(j+s[i]+key[i%len(key)])%256;s[i],s[j]=s[j],s[i]
    i=j=0;out=bytearray()
    for b in data:
        i=(i+1)%256;j=(j+s[i])%256;s[i],s[j]=s[j],s[i]
        out.append(b^s[(s[i]+s[j])%256])
    return bytes(out)

def pdf(path,password=False):
    # Tiny two-page fixture, with optional standard PDF R2 password encryption.
    padding=bytes.fromhex('28bf4e5e4e758a4164004e56fffa01082e2e00b6d0683e802f0ca9fe6453697a')
    ident=hashlib.md5(b'tfile fixture').digest()
    key=None; extra=b''
    if password:
        user=(b'secret'+padding)[:32]; owner=(b'owner'+padding)[:32]
        o=rc4(hashlib.md5(owner).digest()[:5],user)
        key=hashlib.md5(user+o+struct.pack('<i',-4)+ident).digest()[:5]
        u=rc4(key,padding)
        extra=b' /Encrypt 8 0 R /ID [<'+ident.hex().encode()+b'><'+ident.hex().encode()+b'>]'
    def stream(n,text):
        b=f'1 0 0 rg 10 10 80 40 re f BT /F1 16 Tf 20 160 Td ({text}) Tj ET'.encode()
        if key:b=rc4(hashlib.md5(key+n.to_bytes(3,'little')+b'\0\0').digest()[:10],b)
        return b'<< /Length '+str(len(b)).encode()+b' >>\nstream\n'+b+b'\nendstream'
    objects=[b'<< /Type /Catalog /Pages 2 0 R >>',
        b'<< /Type /Pages /Count 2 /Kids [3 0 R 5 0 R] >>',
        b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R /Resources << /Font << /F1 7 0 R >> >> >>',
        stream(4,'FIRST PAGE ONLY'),
        b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 6 0 R /Resources << /Font << /F1 7 0 R >> >> >>',
        stream(6,'SECOND PAGE EXCLUDED'), b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>']
    if password:objects.append(b'<< /Filter /Standard /V 1 /R 2 /Length 40 /O <'+o.hex().encode()+b'> /U <'+u.hex().encode()+b'> /P -4 >>')
    b=bytearray(b'%PDF-1.4\n'); offsets=[0]
    for n,obj in enumerate(objects,1):
        offsets.append(len(b));b.extend(f'{n} 0 obj\n'.encode()+obj+b'\nendobj\n')
    start=len(b);b.extend(f'xref\n0 {len(offsets)}\n0000000000 65535 f \n'.encode())
    for offset in offsets[1:]:b.extend(f'{offset:010} 00000 n \n'.encode())
    b.extend(b'trailer\n<< /Size '+str(len(offsets)).encode()+b' /Root 1 0 R'+extra+b' >>\nstartxref\n'+str(start).encode()+b'\n%%EOF\n')
    path.write_bytes(b)

def main():
    image=shutil.which('magick') or shutil.which('convert')
    if not image:
        print('SKIP: real image conversion (ImageMagick missing)');return
    with tempfile.TemporaryDirectory(prefix='tfile-media-real-') as directory:
        root=Path(directory);out=root/'out'
        def render(path,text=False,ok=True,expected=None,env=None):
            p=subprocess.run([PROBE,str(path),str(out),str(int(text)),'512','384'],capture_output=True,text=True,env=env)
            assert (p.returncode==0)==ok,(path,p.stdout,p.stderr)
            if expected:assert expected in p.stdout,(expected,p.stdout)
            if ok and not text:
                dims=tuple(map(int,re.match(rb'\x1bP[0-9;]*q"1;1;(\d+);(\d+)',out.read_bytes()).groups()))
                assert dims[0]<=512 and dims[1]<=384;return dims
            return p.stdout
        for suffix in ['png','jpg']:
            path=root/f"- 한글 'quote\" [0].{suffix}"
            subprocess.run([image,'-size','120x60','gradient:red-blue',str(path)],check=True)
            assert render(path)==(120,60)
        large=root/'large.png';subprocess.run([image,'-size','2048x1024','gradient:red-blue',str(large)],check=True)
        assert render(large)==(512,256)
        broken=root/'broken.png';broken.write_bytes(b'\x89PNG\r\n\x1a\ncorrupt')
        render(broken,ok=False,expected='Damaged image')
        broken=root/'broken.jpg';broken.write_bytes(b'\xff\xd8\xffgarbage')
        render(broken,ok=False,expected='Damaged image')
        capped=root/'capped.png'
        with capped.open('wb') as f:f.write(b'\x89PNG\r\n\x1a\n');f.truncate(65*1024*1024)
        render(capped,ok=False,expected='64 MiB')
        png=root/'large.png'
        render(png,ok=False,expected='Missing ImageMagick',env={**os.environ,'PATH':'/nonexistent'})
        if shutil.which('pdftoppm') and shutil.which('pdftotext'):
            doc=root/"- 한글 'quote\" [0].pdf";pdf(doc)
            assert render(doc)==(384,384)
            render(doc,text=True);text=out.read_text()
            assert 'FIRST PAGE ONLY' in text and 'SECOND PAGE EXCLUDED' not in text
            locked=root/'encrypted.pdf';pdf(locked,password=True)
            # Prove the encrypted fixture is valid, and readable with its password.
            valid=subprocess.run(['pdftotext','-upw','secret',str(locked),'-'],capture_output=True,text=True,check=True)
            assert 'FIRST PAGE ONLY' in valid.stdout
            render(locked,ok=False,expected='Encrypted PDF')
            corrupt=root/'broken.pdf';corrupt.write_bytes(b'%PDF-1.4\ngarbage')
            render(corrupt,ok=False,expected='Damaged PDF')
            print('PASS: real Poppler first-page image/text, valid encrypted PDF, damaged PDF')
        else:print('SKIP: real PDF conversion (Poppler missing)')
        print('PASS: real ImageMagick PNG/JPEG, special names, 2048x1024 -> 512x256, damaged files/source cap/missing tools; Sixel payload validated, real pixel display unverified')
if __name__=='__main__':main()
