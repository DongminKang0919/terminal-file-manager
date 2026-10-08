"""Deterministic converter doubles; no image decoder or PDF renderer is used here."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

STUB = r'''#!/usr/bin/python3
import json, os, sys, time, resource
from pathlib import Path
name=Path(sys.argv[0]).name
args=sys.argv[1:]
source=next((x for x in args if '/proc/self/fd/' in x),None)
assert source is not None
if source.startswith(('PNG:', 'JPEG:')): source=source.split(':',1)[1]
b=Path(source).read_bytes()
mode=b[8:] if b.startswith(b'\x89PNG') else b[3:] if b.startswith(b'\xff\xd8\xff') else b.split(b'\n',1)[-1]
# A PDF raster stage is passed by its safe private name instead of source FD.
log=os.environ.get('TFILE_MEDIA_TEST_LOG')
if log:
    with open(log,'a') as f: f.write(json.dumps({'tool':name,'args':args,'mode':mode.decode(errors='replace'),'pid':os.getpid()})+'\n')
if mode==b'slow': time.sleep(30)
if mode==b'late': time.sleep(.15)
if mode==b'fail': sys.stderr.write('damaged image\x1b[2J hostile diagnostic');sys.exit(1)
if mode==b'encrypted': sys.stderr.write('Command Line Error: Incorrect password');sys.exit(1)
if mode==b'huge': sys.stdout.buffer.write(b'x'*(3*1024*1024));sys.exit(0)
if name=='pdftoppm':
    assert args[0:5]==['-f','1','-l','1','-singlefile']
    Path(args[-1]+'.png').write_bytes(b'\x89PNG\r\n\x1a\nok');sys.exit(0)
if name=='pdftotext':
    assert args[:4]==['-f','1','-l','1']
    if mode==b'limits':
        print(';'.join(str(resource.getrlimit(k)[0]) for k in [resource.RLIMIT_AS,resource.RLIMIT_CPU,resource.RLIMIT_FSIZE,resource.RLIMIT_CORE]));sys.exit(0)
    if mode==b'zero':sys.exit(0)
    if mode==b'white':sys.stdout.write(' \t\n\r\f');sys.exit(0)
    if mode==b'ff':sys.stdout.write('\f');sys.exit(0)
    if mode==b'indent':sys.stdout.write('  Indented\n\tNext line\n\f');sys.exit(0)
    print('First page text\nSafe \x1b[2J text');sys.exit(0)
if mode==b'bad': sys.stdout.buffer.write(b'\x1b]52;c;bad\x07');sys.exit(0)
if mode==b'overflow': sys.stdout.buffer.write(b'\x1bPq"1;1;6;6!9999~\x1b\\');sys.exit(0)
if mode.startswith(b'shape:'):
    import re
    w,h=map(int,mode[6:].split(b'x'))
    limit=next(x for x in args if re.fullmatch(r'\d+x\d+>',x))
    mw,mh=map(int,limit[:-1].split('x'))
    scale=min(1,mw/w,mh/h); w=max(1,int(w*scale)); h=max(1,int(h*scale))
    frame=f'\x1bP0;0;0q"1;1;{w};{h}#0;2;100;0;0'
    for row in range(0,h,6):
        if row: frame+='-'
        frame+=f'!{w}'+chr(63+(1<<min(6,h-row))-1)
    sys.stdout.buffer.write((frame+'\x1b\\').encode());sys.exit(0)
sys.stdout.buffer.write(b'\x1bP0;0;0q"1;1;6;6#0;2;100;0;0!6~\x1b\\')
'''
# In converter stage two, the raster path is an ordinary PNG: argument.
STUB = STUB.replace("source=next((x for x in args if '/proc/self/fd/' in x),None)",
                    "source=next((x for x in args if '/proc/self/fd/' in x or x.startswith('PNG:')),None)")

if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='tfile-media-tools-') as directory:
        root=Path(directory)
        for tool in ['magick','convert','pdftoppm','pdftotext']:
            (root/tool).write_text(STUB); (root/tool).chmod(0o700)
        subprocess.run([os.environ.get('TFILE_MEDIA_TEST','./tests/media_test'),str(root)],check=True)
