import os
from pathlib import Path
import re
import tempfile
from display_pty import Terminal, shown

for w,h in [(50,9),(80,24),(160,32)]:
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory); dest=root/'dest'; dest.mkdir()
        source=root/'00-한글\nsource'; source.write_text('keep original')
        t=Terminal(directory,w,h)
        def button(label):
            for y in range(h):
                row=t.screen.row(y)
                if label in row: t.click(row.index(label)+2,y); return
            raise AssertionError(label)
        def source_select(): t.send('\x1bOH\x1bOB')
        try:
            source_select(); t.send('\x1b[15~'+str(dest)+'\n')
            assert list(dest.iterdir())==[] # Destination Enter cannot execute.
            t.send('\x1bOHcopy-')
            t.send('\t\t\n') # explicit Paths view
            pw=min(88,w-4); px=(w-pw)//2; ph=h-2
            seen={}
            for _ in range(100):
                match=re.search(r'Lines (\d+)-(\d+)/(\d+)',t.screen.row(ph-2)); assert match
                first,last,total=map(int,match.groups())
                for n in range(first,last+1): seen[n]=''.join(t.screen.rows[2+n-first][px+2:px+pw-2]).rstrip()
                if last==total: break
                t.send('\x1b[6~')
            body=''.join(seen[i] for i in range(1,total+1))
            assert shown(os.fsencode(source)) in body and shown(os.fsencode(dest/('copy-'+source.name))) in body
            t.send('\x1b\t\t'); assert list(dest.iterdir())==[]
            button('[ Copy now ]')
            assert (dest/('copy-'+source.name)).read_text()=='keep original'
            assert source.exists()
            source_select(); t.send('\x1b[15~dest\t\x1bOHbrowse-\t\n')
            t.send('\x1b') # Cancel Browse must retain both typed fields.
            assert 'dest' in '\n'.join(t.screen.row(y) for y in range(h))
            assert 'browse-' in '\n'.join(t.screen.row(y) for y in range(h))
            button('[ Browse ]'); t.send(' ')
            t.send('\t\t\t\n')
            assert (dest/('browse-'+source.name)).exists()
            # Collision retains the form, original data and editable name.
            source_select(); t.send('\x1b[15~\n\n\n')
            assert 'exists' in '\n'.join(t.screen.row(y) for y in range(h)), (w, '\n'.join(t.screen.row(y) for y in range(h)))
            assert source.read_text()=='keep original'
            t.send('\x1b[Z'*4+'\x1bOHfixed-\n\n')
            assert (root/('fixed-'+source.name)).exists()
            source_select(); t.send('\x1b[15~missing-directory\n\n\n')
            assert 'Destination:' in '\n'.join(t.screen.row(y) for y in range(h))
            t.send('\t\t\x1bOH'+'\x1b[3~'*100+'dest\n\x1bOHretry-\n\n')
            assert (dest/('retry-'+source.name)).exists()
            if os.geteuid()!=0:
                source_select(); dest.chmod(0)
                try:
                    t.send('\x1b[15~dest\n\n\n')
                    assert 'Permission denied' in '\n'.join(t.screen.row(y) for y in range(h))
                    assert source.exists()
                finally: dest.chmod(0o700)
                t.send('\n'); assert (dest/source.name).exists()
            # Long directory text and the source's control bytes are never shell-expanded.
            long=dest
            for n in range(4): long=long/('part'+str(n)+'한'*20); long.mkdir()
            source_select(); t.send('\x1b[17~'+str(long)+'\n\n')
            assert source.exists()
            button('[ Move now ]')
            assert not source.exists() and (long/source.name).read_text()=='keep original'
        finally:t.close()
print('PASS: destination-first forms, explicit execution, absolute/relative base, complete path paging, Browse retention/mouse, collision/error retry, permissions, long Unicode paths and raw control names at all sizes')
