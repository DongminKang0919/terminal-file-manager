import os
from pathlib import Path
import tempfile
from display_pty import Terminal
for w,h in [(50,9),(80,24),(160,32)]:
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory); (root/'a-한글').write_text('source'); (root/'taken').write_text('keep')
        t=Terminal(directory,w,h)
        def rename(): t.send('\x1b[20~'+'\x1bOB'*11+'\n')
        try:
            inode=(root/'a-한글').stat().st_ino
            rename(); t.send('\n'); assert (root/'a-한글').stat().st_ino==inode
            rename(); t.send('\x1bOH'+'\x1b[3~'*100+'taken\n')
            assert 'exists' in '\n'.join(t.screen.row(y) for y in range(h))
            assert (root/'taken').read_text()=='keep'
            t.send('-new\n')
            assert not (root/'a-한글').exists() and (root/'taken-new').read_text()=='source'
            assert any('> taken-new' in t.screen.row(y) for y in range(h))
            raw=os.fsencode(directory)+b'/a\xff\n'; fd=os.open(raw,os.O_CREAT|os.O_WRONLY,0o600); os.close(fd)
            t.send('r\x1bOH'); rename(); t.send('\x1bOHprefix-\n')
            assert os.path.exists(os.fsencode(directory)+b'/prefix-a\xff\n')
        finally:t.close()
print('PASS: direct menu rename no-op, retained collision/edit, exact selection and original invalid/control bytes at 50x9/80x24/160x32')
