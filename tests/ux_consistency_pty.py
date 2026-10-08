"""Behavioral UI consistency, using isolated files and the real input path."""
from pathlib import Path
import tempfile
from display_pty import Terminal

F3='\x1bOR'; F8='\x1b[19~'; BTAB='\x1b[Z'; DOWN='\x1bOB'
for w,h in [(50,9),(100,24)]:
    with tempfile.TemporaryDirectory(prefix='tfile-ux-') as directory:
        root=Path(directory)
        (root/'a-name').write_text('short\n')
        (root/'b-name').write_text('other\n')
        t=Terminal(directory,w,h)
        def body():return '\n'.join(t.screen.row(y) for y in range(h))
        try:
            assert 'Marked: 0' in t.screen.row(h-2)
            assert 'Dotfiles: on' in t.screen.row(h-2)
            t.send(' '+DOWN)  # cursor changes independently of marked target
            assert 'Marked: 1' in t.screen.row(h-2)
            t.send(F3+'b-name\n');t.send('\t\n')
            assert 'Name contains:' in body()  # Enter on Search reopens input
            t.send('\n');t.send(BTAB+'\n')
            assert 'Name contains:' not in body() and 'Search' not in t.screen.row(2)
            assert 'Marked: 1' in t.screen.row(h-2)
            t.send(F3+'no-match\n')
            rw=min(90,w-8);rx=(w-rw)//2
            t.click(rx+15,h-3);t.send('\n')
            assert 'No results collected' in body()  # disabled Open does nothing
            t.send('\t\t'+BTAB+'\n')
            assert 'Name contains:' in body()  # Open is skipped in Tab cycle
            t.send('\x1b')
            # Clear marks, then Shift+Tab reaches Delete from the safe Cancel default.
            t.send('\x1b[20~'+DOWN*15+'\n'+F8)
            assert 'Permanently delete' in body()
            t.send(BTAB+'\n')
            assert not (root/'b-name').exists() and (root/'a-name').exists()
            assert '[Success]' in t.screen.row(h-2)
            t.send('!\n')
            assert '[Success]' not in t.screen.row(h-2)  # Enter acknowledges focused Ack
        finally:t.close()
print('PASS: cursor/marked distinction; search Tab/Shift+Tab/Enter; disabled Open; permanent delete Shift+Tab; acknowledgement and modal restoration at 50x9/100x24')
