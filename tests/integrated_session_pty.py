"""Connected user session with real files/tools and isolated persistent settings.
PTY cells/processes are checked; this does not inspect physical pixels/ghosting.
"""
import os
from pathlib import Path
import re
import shutil
import sys
import struct
import tempfile
import zlib
import display_pty
from preview_latency_pty import until, text
from process_probe import process_name

HOME='\x1bOH'; DOWN='\x1bOB'; DELETE='\x1b[3~'
F3='\x1bOR'; F5='\x1b[15~'; F6='\x1b[17~'; F7='\x1b[18~'; F8='\x1b[19~'; F9='\x1b[20~'
display_pty.BINARY=os.path.abspath(os.environ.get('TFILE_SESSION_BINARY','tfile'))

class EditorScreen(display_pty.Screen):
    def csi(self,params,command):
        if params.startswith(('>','<')) or command in ('n','c','m') or (command=='q' and params.endswith(' ')) or (command=='p' and params=='0%'):return
        super().csi(params,command)

def png(path):
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',8,8,8,2,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+b'\x30\x90\xd0'*8)*8))+chunk(b'IEND',b''))

def pdf(path):
    stream=b'BT /F1 12 Tf 10 100 Td (PDF01) Tj ET\n'
    objects=[b'<< /Type /Catalog /Pages 2 0 R >>',b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
        b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>',
        f'<< /Length {len(stream)} >>\nstream\n'.encode()+stream+b'endstream',b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>']
    data=b'%PDF-1.4\n';offsets=[]
    for i,obj in enumerate(objects,1):offsets.append(len(data));data+=f'{i} 0 obj\n'.encode()+obj+b'\nendobj\n'
    start=len(data);data+=b'xref\n0 6\n0000000000 65535 f \n'+b''.join(f'{n:010} 00000 n \n'.encode() for n in offsets)
    data+=f'trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n{start}\n%%EOF\n'.encode();path.write_bytes(data)

for width,height in ([(100,24)] if '--normal' in sys.argv else [(50,9),(100,24)]):
    with tempfile.TemporaryDirectory(prefix='tfile-integrated-') as temp:
        base=Path(temp);root=base/'root';root.mkdir();source=root/'src';source.mkdir();dest=root/'dest';dest.mkdir()
        nested=source/'nested';nested.mkdir();(nested/'needle.txt').write_text('NEEDLE01\n')
        for name,body in [('alpha.txt','ALPHA01\n'*150),('beta.txt','BETA01\n'),('gamma.txt','GAMMA01\n'),('delta.txt','DELTA01\n'),('.hidden','hidden')]:(source/name).write_text(body)
        png(source/'image.png');pdf(source/'document.pdf')
        (source/'empty').touch();(source/'binary').write_bytes(b'\0raw')
        (source/'locked').write_text('private');(source/'locked').chmod(0)
        home=base/'home';home.mkdir();(home/'.vimrc').write_text('set nocompatible noswapfile shortmess+=I\n')
        config=base/'config/tfile';config.mkdir(parents=True);settings=config/'settings.conf';settings.write_text('version=1\nimage=0\n')
        os.environ.update(HOME=str(home),XDG_CONFIG_HOME=str(base/'config'),XDG_DATA_HOME=str(base/'data'),XDG_CACHE_HOME=str(base/'cache'))
        for key in ['VIMINIT','EXINIT','TFILE_SIXEL','TFILE_CELL_PIXELS']:os.environ.pop(key,None)
        t=display_pty.Terminal(str(source),width,height)
        def find(name):t.send('\x06'+name+'\n')
        def menu(index):
            marked=re.search(r'Marked: (\d+)',text(t))
            steps=index-(1 if index>11 and marked and int(marked[1])>1 else 0)
            t.send(F9+HOME+DOWN*steps+'\n')
        def button(label):
            for y in range(height):
                row=t.screen.row(y)
                if label in row:t.click(row.index(label)+2,y);return
            raise AssertionError(text(t))
        def marks(n):assert f'Marked: {n}' in text(t),text(t)
        def details():
            t.send('!');rows={}
            while True:
                match=re.search(r'Lines (\d+)-(\d+)/(\d+)',text(t));assert match,text(t)
                first,last,total=map(int,match.groups());pw=min(78,width-8);px=(width-pw)//2
                for n in range(first,last+1):rows[n]=''.join(t.screen.rows[2+n-first][px+2:px+pw-2]).strip()
                if last==total:break
                t.send('\x1b[6~')
            t.send('\x1b');return ''.join(rows[n] for n in range(1,total+1)).replace(' ','')
        try:
            # Mouse selection -> directory -> sort/filter -> recursive search -> history.
            find('nested');row=next(y for y in range(4,height-3) if 'nested' in t.screen.row(y))
            t.click(5,row);t.send('\n');assert 'needle.txt' in text(t)
            t.send('\x7f');t.send(F7+HOME+DOWN*3+'\n\x1b')
            assert ('Size+' if width<80 else 'Size ascending') in text(t),text(t);t.send(F7+HOME+DOWN*3+'\n\n\n\x1b');assert ('Name+' if width<80 else 'Name ascending') in text(t),text(t)
            t.send('f\nalpha\n');assert 'beta.txt' not in text(t)
            t.send(F3+'needle\n');until(t,lambda:'Complete:' in text(t));button('[ Open ]')
            assert 'needle.txt' in text(t) and 'Filter:' not in text(t),text(t)
            t.send('[');assert '/src' in text(t);t.send(']');assert 'needle.txt' in text(t);t.send('[')
            # Freeze marks before first dual creation; peer path determines target.
            for name in ['alpha','beta']:find(name);t.send(' ')
            marks(2);menu(18);t.send('\t\x7f'+HOME+'\n');assert '/dest' in text(t),text(t)
            t.send('\t'+F5+'\n');button('[ Copy 2 items ]')
            until(t,lambda:(dest/'beta.txt').exists());marks(0)
            result=details();assert 'Target1/2:Success' in result and 'Target2/2:Success' in result,result
            t.send('\t');assert 'alpha.txt' in text(t);t.send('\t')
            for name in ['delta','gamma']:find(name);t.send(' ')
            t.send(F6+'\n');button('[ Move 2 items ]')
            until(t,lambda:(dest/'gamma.txt').exists());assert not (source/'gamma.txt').exists() and not (source/'delta.txt').exists()
            t.send('\t');find('gamma');assert 'gamma.txt' in text(t),text(t);t.send('\t');menu(16)
            # PNG/PDF/text switching and page cancel; PDF uses real text fallback when installed.
            find('alpha');until(t,lambda:'ALPHA01' in text(t));t.send('\n\x1b[6~\x1b')
            find('image');until(t,lambda:('[i] Disabled' if height==9 else '[i] Preview disabled') in text(t))
            find('document')
            if shutil.which('pdftotext'):
                if height==9:
                    until(t,lambda:'Tab: Preview' in text(t));t.send('\t'+DOWN)
                until(t,lambda:'PDF01' in text(t));t.send('\x1b')
            else:print('SKIP: real pdftotext fallback unavailable',flush=True)
            t.send(HOME+DOWN*6+HOME+DOWN*3+HOME)
            find('alpha');until(t,lambda:'ALPHA01' in text(t));assert 'PDF01' not in text(t)
            for name,state in [('empty','[i] Empty file'),('binary','[i] Unavailable' if height==9 else '[i] Preview unavailable')]:
                find(name);until(t,lambda:state in text(t))
            if os.geteuid()!=0:
                find('locked');until(t,lambda:('[!] Error' if height==9 else '[!] Preview failed') in text(t))
            else:print('SKIP: real permission-denied preview when running as root',flush=True)
            find('alpha');until(t,lambda:'ALPHA01' in text(t))
            # Installed Vim edits cursor, returns to usable mouse/preview/list state.
            if shutil.which('vim'):
                t.screen=EditorScreen(height,width);t.send('r');until(t,lambda:'ALPHA01' in text(t))
                t.send('e')
                def vim_running():
                    for pid in Path(f'/proc/{t.proc.pid}/task/{t.proc.pid}/children').read_text().split():
                        if (process_name(pid) or '').startswith('vim'):return True
                    return False
                until(t,vim_running);t.send('gg0iEDIT01\n\x1b:wq\r')
                until(t,lambda:not vim_running() and 'EDIT01' in text(t));assert (source/'alpha.txt').read_text().startswith('EDIT01\n')
                row=next(y for y in range(4,height-3) if 'alpha.txt' in t.screen.row(y));t.click(5,row)
            else:print('SKIP: installed Vim edit/save/quit unavailable',flush=True)
            # External atomic replacement must invalidate cached preview after refresh.
            replacement=source/'replacement';replacement.write_text('NEW01\n');replacement.replace(source/'alpha.txt')
            t.send('r');until(t,lambda:'NEW01' in text(t));assert 'EDIT01' not in text(t)
            # Trash partial success with second source removed externally after marking.
            for name in ['z-trash-one','z-trash-two']:(source/name).write_text(name)
            t.send('r')
            for name in ['z-trash-one','z-trash-two']:find(name);t.send(' ')
            (source/'z-trash-two').unlink();t.send('t');button('[ Trash ]')
            until(t,lambda:not (source/'z-trash-one').exists())
            result=details();assert 'Target1/2:Success' in result and 'Target2/2:Failed' in result,result
            assert any(p.read_text()=='z-trash-one' for p in (base/'data/Trash/files').iterdir())
            # Default cancel and mouse permanent delete remain distinct from Trash.
            victim=source/'z-delete';victim.write_text('delete');t.send('ur');find('z-delete')
            t.send(F8+'\n');assert victim.exists();t.send(F8);button('[ Delete ]');until(t,lambda:not victim.exists())
            assert 'Success' in details()
            # Save explicit options and restart in the same isolated HOME/XDG.
            t.send(F7+HOME+'\n'+DOWN*2+'\n'+DOWN*4+'\n')
            assert 'Saved startup defaults' in text(t);t.send('\x1b')
            saved=settings.read_text();assert 'hidden=0' in saved and 'wheel=3' in saved and 'mode=1' in saved,saved
        finally:t.close()
        t=display_pty.Terminal(str(source),width,height)
        try:
            assert '.hidden' not in text(t) and 'Dotfiles:off' in text(t).replace(' ','') and ('Name+' if width<80 else 'Name ascending') in text(t),text(t)
            t.send(F7);assert 'Wheel scroll: 3' in text(t);t.send('\x1b')
        finally:t.close()
        print(f'PASS connected session {width}x{height}: navigation/sort/filter/search/history, marks/dual/copy/move/results, PNG/PDF/text, Vim, replacement, partial Trash/delete/cancel, settings restart',flush=True)
