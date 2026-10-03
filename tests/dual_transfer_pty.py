"""Editable opposite defaults, modal retention and recovery on real terminals."""
import os
from pathlib import Path
import tempfile
from display_pty import Terminal

HOME='\x1bOH'; DOWN='\x1bOB'; BACK='\x7f'
F5='\x1b[15~'; F6='\x1b[17~'; F9='\x1b[20~'
for w,h in [(50,9),(80,24),(160,32)]:
    with tempfile.TemporaryDirectory(prefix='tfile-dual-transfer-') as directory:
        root=Path(directory); a=root/'A'; b=root/'B'; c=root/'C'
        for p in [a,b,c]: p.mkdir()
        (a/'source').write_text('original'); (b/'peer').write_text('peer')
        t=Terminal(directory,w,h)
        def body(): return '\n'.join(t.screen.row(y) for y in range(h))
        def menu(n): t.send(F9+HOME+DOWN*n+'\n')
        def button(label):
            for y in range(h):
                row=t.screen.row(y)
                if label in row: t.click(row.index(label)+2,y); return
            raise AssertionError(body())
        def replace(value): t.send(HOME+'\x1b[3~'*600+value)
        def run(): button('[ Copy now ]')
        try:
            t.send(HOME+'\n'); menu(18) # Left A, Right A
            t.send('\t'+BACK+HOME+DOWN+'\n'+' '+ '\t') # Right B marked peer; Left A
            t.send(F5); assert '/B' in body();t.send('\n\n')
            assert not (b/'source').exists() # explicit button only
            run();assert (b/'source').read_text()=='original' and (a/'source').exists()
            t.send('\t');assert 'M:1' in t.screen.row(h-2);t.send('\t')
            # Same name collision keeps the form. Editing relative to frozen A
            # must not navigate the opposite B panel.
            t.send(F5); run(); assert 'exists' in body()
            t.send('\x1b[Z'*5);replace('../C');run();assert (c/'source').exists()
            t.send('\t');assert '/B' in body();t.send('\t')
            # Destination disappears after opening: preserve input and selection.
            t.send(F5);(b/'source').unlink();(b/'peer').unlink();b.rmdir()
            run();assert 'Destination:' in body() and (a/'source').exists()
            t.send('\x1b');assert not b.exists()
            # Opening with a missing opposite directory gives an immediate warning,
            # Browse stays at that path; explicit p selects a valid replacement.
            t.send(F5);assert 'Destination:' in body();t.send('\t\t\n')
            assert '/B' in body();t.send('p');replace(str(root));t.send('\n ')
            run();assert (root/'source').exists() and not b.exists()
            # Current path revalidation can recover without navigating peer.
            b.mkdir()
            if os.geteuid()!=0:
                b.chmod(0)
                try:
                    t.send(F6);assert 'Destination:' in body()
                    button('[ Move now ]');assert 'Permission denied' in body() and (a/'source').exists()
                finally: b.chmod(0o700)
                button('[ Move now ]')
            else:
                t.send(F6);button('[ Move now ]')
            assert not (a/'source').exists() and (b/'source').read_text()=='original'
            t.send('\t');assert '/B' in body();t.send('\t')
            # Explicit Files only and Files + Preview retain old blank default.
            (a/'single').write_text('single');t.send('r')
            for mode in [17,16]:
                menu(mode);t.send(F5)
                # At minimum height the input is visible with the name; the peer
                # path must not become the default when explicitly hidden.
                assert str(b) not in body();t.send('\x1b')
            assert (a/'single').exists()
        finally: t.close()
print('PASS: opposite defaults, explicit mouse execution, peer marks/path, relative edits, collision, deleted-path retry/Browse, move, single/preview defaults at 50x9/80x24/160x32')
