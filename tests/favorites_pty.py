"""Favorites UI and restart; disposable directories/config only, no visual claim."""
import os
from pathlib import Path
import tempfile
from display_pty import Terminal

with tempfile.TemporaryDirectory(prefix='tfile-favorites-pty-') as d:
    base=Path(d); root=base/'root'; root.mkdir(); child=root/'child';child.mkdir()
    config=base/'config';os.environ['XDG_CONFIG_HOME']=str(config)
    t=Terminal(str(root))
    try:
        t.send('ba\x1bOH'+'\x1b[3~'*100+'즐겨찾기\n')
        assert 'Favorite saved' in '\n'.join(t.screen.row(i) for i in range(24))
        t.send('r\x1bOH'+'\x1b[3~'*100+'이름 변경\n\x1b')
        t.send('\n'); assert 'child' in t.screen.row(1)
        t.send('b\n'); assert str(root) in t.screen.row(1) and 'child' not in t.screen.row(1)
        t.send('[');assert 'child' in t.screen.row(1)
        moved=base/'moved-root';root.rename(moved)
        t.send('b\n')
        assert 'Open directory' in '\n'.join(t.screen.row(i) for i in range(24))
        t.send('\x1b');assert 'child' in t.screen.row(1)
        moved.rename(root)
        t.send('\x1b[20~'+'\x1bOB'*18+'\n\t'+'b\n')
        assert 'child' in t.screen.row(1)[:50] and 'child' not in t.screen.row(1)[50:]
    finally:t.close()
    assert not (config/'tfile/settings.conf').exists()
    assert (config/'tfile/favorites.conf').exists()
    t=Terminal(str(child))
    try:
        t.send('b');assert '이름 변경' in '\n'.join(t.screen.row(i) for i in range(24))
        t.send('d');assert 'not deleted' in '\n'.join(t.screen.row(i) for i in range(24))
        t.send('\x1b');assert root.exists() and child.exists()
    finally:t.close()
print('PASS: favorites add/rename/go/history/unregister/restart, independent startup storage (PTY)')
