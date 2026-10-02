from pathlib import Path
import tempfile
from display_pty import Terminal
for w,h in [(50,9),(80,24),(160,32)]:
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory)
        for name in ['a-한글','b-한글','jkqr','ćŗšƚ']+[f'z{i:02}' for i in range(30)]: (root/name).write_text('preview')
        t=Terminal(directory,w,h)
        def selected(): return next(t.screen.row(y) for y in range(h) if t.screen.rows[y][1]=='>')
        try:
            t.send('\x1bOF\x1b[19~\x1b'); before=selected()
            t.send('\x06한글'); assert 'a-한글' in selected()
            assert 'Find:' in t.screen.row(h-2)
            t.send('없는'); assert 'No match:' in t.screen.row(h-2) and 'a-한글' in selected()
            t.send('\x7f\x7f\x1bOB'); assert 'b-한글' in selected()
            t.send('\x1bOA'); assert 'a-한글' in selected()
            t.send('\x1b'); assert selected()==before and 'Delete cancelled' in t.screen.row(h-2)
            t.send('\x06jkqr\n'); assert 'jkqr' in selected() and 'Enter: Open' in t.screen.row(h-1)
            assert 'Delete cancelled' in t.screen.row(h-2)
            t.send('\x06ćŗšƚ\n'); assert 'ćŗšƚ' in selected()
            t.send('\x06한글\n'); assert 'a-한글' in selected()
            assert 'Enter: Open' in t.screen.row(h-1) # Enter selects, never opens
            # F9 entry is reachable by scrolling past the existing commands.
            t.send('\x1b[20~'+'\x1bOB'*10+'\n')
            assert 'Find:' in t.screen.row(h-2)
            t.send('\x1b')
            assert len(list(root.iterdir()))==34
        finally: t.close()
    with tempfile.TemporaryDirectory() as directory:
        t=Terminal(directory,w,h)
        try:
            t.send('\x06q'); assert 'No match:' in t.screen.row(h-2)
            t.send('\n'); assert 'No visible items' in '\n'.join(t.screen.row(y) for y in range(h))
        finally:t.close()
print('PASS: loaded-list find Unicode editing/no match/wrap/commit/cancel, literal command letters, menu and empty list at 50x9/80x24/160x32')
