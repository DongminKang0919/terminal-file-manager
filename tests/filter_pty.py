"""Panel filter interactions on disposable fixtures; PTY cells, no visual claim."""
from pathlib import Path
import tempfile
from display_pty import Terminal
for w,h in [(50,9),(100,24)]:
    with tempfile.TemporaryDirectory(prefix='tfile-filter-pty-') as d:
        root=Path(d)
        for name in ['alpha.txt','beta.jpg','한글.txt']: (root/name).write_text('body')
        t=Terminal(d,w,h)
        def text():return '\n'.join(t.screen.row(y) for y in range(h))
        try:
            t.send(' ');t.send('f\n'+'한글\n')
            assert 'Filter' in text() and '한글.txt' in text() and 'beta.jpg' not in text()
            assert 'Marked: 0' in text() or 'marks cleared' in text()
            t.send('r');assert 'Filter' in text() and 'beta.jpg' not in text()
            t.send('f\x1bOB\n\x1bOH'+'\x1b[3~'*20+'*.jpg\n')
            assert 'beta.jpg' in text() and 'alpha.txt' not in text()
            t.send('f\n\x1bOH'+'\x1b[3~'*20+'absent\n');assert 'No filter matches' in text()
            t.send('f'+'\x1bOB'*2+'\n');assert 'Filter active' not in text()
            if w>=80:
                t.send('\x1b[20~'+'\x1bOB'*18+'\n')
                t.send('f\nalpha\n\t');assert 'alpha.txt' in text() and 'beta.jpg' in text()
                assert 'Left Filter:' in text() and 'Right Filter:' not in text()
        finally:t.close()
print('PASS: filter prompts/clear, Unicode/glob/no-match/marks/refresh and independent dual panel at 50x9/100x24')
