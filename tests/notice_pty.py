"""Status/result lifecycle using the real keyboard and mouse input paths."""
from pathlib import Path
import tempfile
from display_pty import Terminal

for w,h in [(50,9),(100,24)]:
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory)/('경로'*35)/('진단'*35)
        root.mkdir(parents=True)
        for i in range(30): (root/f'{i:02}-한글').write_text('body\n'*40)
        t=Terminal(str(root),w,h)
        def status(): return t.screen.row(h-2)
        def body(): return '\n'.join(t.screen.row(y) for y in range(h))
        def result():
            assert 'Recent operation result' in body()
        try:
            assert 'Shown: 30 | Marked: 0 | Dotfiles: on' in status()
            t.send('!'); result(); assert 'No recent file operation result.' in body()
            t.send('\x1b')
            t.send('\x1bOF\x1b[19~\x1b')
            assert '[Cancelled]' in status() and 'Shown: 30' in status()
            assert 'cursor' in t.screen.row(h-3)
            before=[t.screen.row(y) for y in range(2,h-2)]
            t.click(w-5,h-2); result()
            t.send(f'\x1b[<65;{(w-min(78,w-8))//2+4};4M'); assert 'Lines 2-' in body()
            t.send(f'\x1b[<64;{(w-min(78,w-8))//2+4};4M'); assert 'Lines 1-' in body()
            t.send('\x1bOF'); assert 'End' in body()
            t.send('\x1bOH\x1b')
            assert [t.screen.row(y) for y in range(2,h-2)]==before
            t.send('\x1bOP\x1b'); assert '[Cancelled]' in status(), body()
            t.send('\x1b[18~\x1b'); assert '[Cancelled]' in status()
            t.send('\x06한글\x1b'); assert '[Cancelled]' in status()
            t.send('z'); assert '[Cancelled]' not in status() and 'Shown: 30' in status()
            t.send('!'); result()
            t.click((w-min(78,w-8))//2+min(78,w-8)-4,1)
            assert 'Recent operation result' not in body()
            t.send('!'); result()
            # Ack button is in the fixed footer of a centered LINES-2 window.
            t.click((w-min(78,w-8))//2+4,h-3)
            assert 'Recent operation result' not in body() and '[Cancelled]' not in status()
            # F9 entries follow the existing twelve entries.
            t.send('\x1b[20~'+'\x1bOB'*12+'\n'); result(); t.send('\x1b')
            # Successful rename replaces cancellation; an actual collision replaces success.
            t.send('\x1b[20~'+'\x1bOB'*11+'\n')
            t.send('\x1bOHnew-\n')
            assert '[Success]' in status() and 'Shown: 30' in status()
            t.send('\x1b[20~'+'\x1bOB'*11+'\n')
            t.send('\x1bOH'+'\x1b[3~'*100+'00-한글\n')
            assert 'exists' in body(); t.send('\x1b')
            assert '[Error]' in status() and 'Shown: 30' in status()
            assert 'cursor' in t.screen.row(h-3)
            t.send('\x1b[19~\x1b')  # Closing a confirmation preserves the unread error.
            assert '[Error]' in status() and len(list(root.iterdir()))==30
            t.send('!'); result(); assert 'File operation: Error' in body()
            t.send('a'); assert '[Error]' not in status()
        finally: t.close()
print('PASS: status summary, keyboard/mouse/menu details, scroll, dismiss/ack/reopen/replacement and retained alerts at 50x9/100x24')
