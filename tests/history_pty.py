from pathlib import Path
import tempfile
from display_pty import Terminal
for w,h in [(50,9),(80,24),(160,32)]:
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory); (root/'A').mkdir(); (root/'B').mkdir()
        for n in range(45): (root/'A'/f'{n:02}-한글').write_text('body')
        t=Terminal(str(root/'A'),w,h)
        split=w*3//5 if w<80 else w*11//20
        def left(): return [''.join(t.screen.rows[y][:split]) for y in range(2,h-2)]
        try:
            t.send('\x1bOF'); saved=left()
            t.send('\x7f\x1bOB\n') # parent, B
            assert 'No visible items' in '\n'.join(t.screen.row(y) for y in range(h))
            t.send('[['); assert left()==saved
            t.send(']'); assert any('> B' in t.screen.row(y) for y in range(h))
            t.send(']'); assert 'No visible items' in '\n'.join(t.screen.row(y) for y in range(h))
            t.send('[['); assert left()==saved
        finally: t.close()
print('PASS: Back/Forward restores exact selected Unicode item and list viewport at 50x9/80x24/160x32')
