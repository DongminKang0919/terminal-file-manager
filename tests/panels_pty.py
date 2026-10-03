"""Real dual-list navigation, selection, modal operations and shared hit geometry."""
import fcntl
import os
from pathlib import Path
import signal
import struct
import tempfile
import termios
from display_pty import Terminal, Screen

HOME='\x1bOH'; END='\x1bOF'; DOWN='\x1bOB'; PGDN='\x1b[6~'; BACK='\x7f'
F2='\x1bOQ'; F3='\x1bOR'; F5='\x1b[15~'; F6='\x1b[17~'; F8='\x1b[19~'; F9='\x1b[20~'
for width,height in [(50,9),(80,24),(160,32)]:
    with tempfile.TemporaryDirectory(prefix='tfile-panels-pty-') as directory:
        root=Path(directory); a=root/'00-A'; b=root/'01-B'; a.mkdir(); b.mkdir()
        for parent in [a,b]:
            for i in range(24): (parent/f'f{i:02}한글').write_text(f'{parent.name}/{i}')
            (parent/'.hidden').write_text('hidden')
        t=Terminal(str(root),width,height)
        w,h=width,height
        def body(): return '\n'.join(t.screen.row(y) for y in range(h))
        def menu(index): t.send(F9+HOME+DOWN*index+'\n')
        def active(side): assert f'{side}* /' in t.screen.row(h-2),body()
        def marked(n): assert f'M:{n}' in t.screen.row(h-2),body()
        def button(label):
            for y in range(h):
                row=t.screen.row(y)
                if label in row: t.click(row.index(label)+2,y); return
            raise AssertionError(body())
        def find(name): t.send('\x06'+name+'\n')
        def destination(value): t.send(HOME+'\x1b[3~'*600+str(value)+'\n')
        def panel(side):
            x=0 if w<80 or side=='Left' else w//2
            pw=w if w<80 else w//2 if side=='Left' else w-w//2
            return '\n'.join(''.join(t.screen.rows[y][x:x+pw]) for y in range(1,h-2))
        def resize(nw,nh):
            if (nw,nh)!=(t.screen.width,t.screen.height): t.screen=Screen(nh,nw)
            fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',nh,nw,0,0))
            os.kill(t.proc.pid,signal.SIGWINCH);t.read()
        try:
            # Left keeps its original path, cursor and marks on first dual entry.
            t.send(HOME+'\n'); find('f03');t.send(' ')
            menu(18);active('Left');marked(1)
            if w>=80:
                assert '00-A' in panel('Left') and '00-A' in panel('Right')
                assert ':' in panel('Right') and '>' in panel('Left')
                assert 'S:25' in panel('Left') and 'M:1' in panel('Left')
            t.send('\t');active('Right');marked(0)
            t.send(BACK+HOME+DOWN+'\n');find('f07');t.send(' ');marked(1)
            if w>=80: assert '00-A' in panel('Left') and '01-B' in panel('Right')
            t.send('\x1b[18~'+DOWN*3+'\n\x1b') # Right-only sort
            assert 'Size+' in panel('Right') and (w<80 or 'Name+' in panel('Left'))
            t.send('\x1b[18~\n\x1b');assert 'H:0' in panel('Right') # Right-only hidden filter
            t.send('\t');active('Left');marked(1);assert 'Name+' in panel('Left') and 'H:1' in panel('Left')
            t.send('\x1b[Z');active('Right');marked(1)
            # Back/forward and search result opening belong to Right.
            t.send('[');assert '00-A' in panel('Right');t.send(']');assert '01-B' in panel('Right')
            t.send(F3+'f11\n');t.send('\n');active('Right');assert 'f11한글' in panel('Right')
            t.send(HOME+PGDN+END);assert 'f23한글' in panel('Right')
            # Popup Esc preserves active side/focus. Enter on a file opens preview.
            t.send('\x1bOP\x1b');active('Right')
            find('f07');t.send(' ');marked(1)
            t.send('\n');assert 'Preview' in body() and 'Right* /' not in body(),body()
            t.send('\x1b');menu(18);active('Right');marked(1)
            t.send('\t');active('Left');marked(0) # Explicit hide cleared only Left marks.
            find('f03');t.send(' ');marked(1)
            # Narrowing is temporary: both sides' marks survive, hidden hit areas do not.
            resize(50,9);w,h=50,9;active('Left');marked(1)
            t.send('\t');active('Right');marked(1)
            t.click(30,4);active('Right')
            resize(160,32);w,h=160,32;active('Right');marked(1)
            t.click(3,4);active('Left');marked(1)
            assert 'M:1' in panel('Right')
            t.click(w//2+3,4);active('Right');marked(1)
            # Different paths: the opposite directory is never an automatic destination.
            t.send('\t'+F6)
            base_line=next(t.screen.row(y) for y in range(h) if 'Base:' in t.screen.row(y))
            to_line=next(t.screen.row(y) for y in range(h) if 'To:' in t.screen.row(y))
            assert '00-A' in base_line and '01-B' not in to_line
            button('[ Cancel ]');active('Left');marked(1);t.send('\t');active('Right')
            # Same path on both sides: refresh source and peer once and reconcile marks.
            t.send(BACK+HOME+'\n');assert '00-A' in panel('Right');menu(15)
            find('f03');t.send(' ');marked(1)
            t.send('\t');active('Left');find('f03')
            t.send(F6);assert 'Move' in body();button('[ Browse ]');t.send('\x1b')
            # No automatic opposite-path destination. Base remains the source directory.
            assert '00-A' in body()
            button('[ Cancel ]');active('Left');marked(1)
            # Mark two Left files; a Right mark must never enter batch targets.
            find('f04');t.send(' ');marked(2)
            (b/'f03한글').unlink();(b/'f04한글').unlink()
            t.send(F5);assert 'Batch destination' in body();destination('missing')
            assert 'Destination:' in body();destination(b)
            assert 'Batch copy confirmation' in body();t.send('\n')
            assert 'Batch destination' in body();destination(b)
            t.send('\t\n');active('Left');marked(0)
            assert (b/'f03한글').read_text()=='00-A/3' and (b/'f04한글').read_text()=='00-A/4'
            t.send('\t');active('Right');marked(1)
            # Rename is active-only; both same-directory lists follow new raw identity.
            menu(15);find('f05');menu(11)
            t.send(HOME+'renamed-\n');active('Right')
            assert (a/'renamed-f05한글').exists() and 'renamed-f05한글' in panel('Right')
            t.send('\t');active('Left');find('renamed-');assert 'renamed-f05한글' in panel('Left')
            t.send(F8+'\t\n');assert not (a/'renamed-f05한글').exists();active('Left')
            t.send('\t\x06renamed-\x1b');assert 'renamed-f05한글' not in panel('Right')
            # Explicit single-list mode retains Right and hides/clears Left only.
            find('f09');t.send(' ');marked(1);menu(17)
            assert 'Files' in body() and '01-B' not in t.screen.row(1)
            menu(18);active('Right');marked(1)
            t.send('\t');marked(0)
            # Resize while destination/picker/review is open closes all modals.
            find('f10');t.send(' '+DOWN+' ');t.send(F5)
            button('[ Browse ]');resize(80,24);w,h=80,24
            assert 'Batch destination' not in body() and 'Choose destination' not in body();active('Left');marked(2)
            if os.geteuid()!=0:
                menu(17);a.chmod(0)
                try:
                    menu(18);active('Left');t.send('\t');active('Right')
                    assert 'STALE' in panel('Right')
                    t.send(F2+F5+F8);assert 'Stale panel' in t.screen.row(h-2)
                    assert 'New -' not in body() and 'Move' not in body() and 'Copy' not in body() and 'confirmation' not in body()
                finally: a.chmod(0o700)
                t.send('r');assert 'STALE' not in panel('Right')
                t.send('\t');active('Left');marked(2)
            else: print('SKIP: real permission failure for hidden panel (root)')
        finally: t.close()
print('PASS: dual keyboard/mouse/geometry/resizing, independent path/sort/hidden/history/find/search/marks, preview and popup return, active-only batch/rename/delete and retained destination flow at 50x9/80x24/160x32')
