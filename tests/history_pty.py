from pathlib import Path
import tempfile
import os
import pty
import tty
from display_pty import Terminal, Screen
# PTYs pass bytes through; a terminal emulator must generate these Alt CSI frames.
master,slave=pty.openpty()
try:
    tty.setraw(slave)
    for sequence in [b'\x1b[1;3D',b'\x1b[1;3C',b'\x1b[D',b'\x1b[C']:
        os.write(master,sequence)
        assert os.read(slave,64)==sequence
finally:
    os.close(master); os.close(slave)

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
            t.send(']]')
            for kb,kf in [('[',']'),('\x1b[1;3D','\x1b[1;3C')]:
                for mouse_back,mouse_forward in [(False,False),(True,True),(False,True),(True,False)]:
                    for _ in range(2):
                        t.click(2,1) if mouse_back else t.send(kb)
                    assert left()==saved
                    for _ in range(2):
                        if mouse_forward:
                            row=t.screen.row(1)
                            t.click(row.index('[Forward >]')+3 if '[Forward >]' in row else 6,1)
                        else: t.send(kf)
                    assert 'No visible items' in '\n'.join(t.screen.row(y) for y in range(h))
        finally: t.close()
print('PASS: Back/Forward restores exact selected Unicode item and list viewport at 50x9/80x24/160x32')

# Exercise the real event loop: nested A -> B -> C, all four input mixes.
for w,h in [(50,9),(100,24),(160,32)]:
    with tempfile.TemporaryDirectory() as directory:
        a=Path(directory)/'A'; b=a/'B'; c=b/'C'; c.mkdir(parents=True)
        (b/'D').mkdir()
        t=Terminal(str(a),w,h)
        try:
            first=4 if h<12 else 5
            def path(): return t.screen.row(1)
            def back(): t.click(2,1)
            def forward(): t.click(path().index('[Forward >]')+3 if '[Forward >]' in path() else 6,1)
            def mouse_open():
                t.click(3,first); t.click(3,first)
            def at(p): assert path().rstrip().endswith(str(p)),path()
            t.send('\x1bOC\x1bOC'); at(c) # Plain Right opens directories.
            for kb,kf in [('[',']'),('\x1b[1;3D','\x1b[1;3C')]:
                for mouse_back,mouse_forward in [(False,False),(True,True),(False,True),(True,False)]:
                    back() if mouse_back else t.send(kb)
                    at(b)
                    forward() if mouse_forward else t.send(kf)
                    at(c)
            t.send(']'); at(c); assert 'No forward history' in t.screen.row(h-2)
            # Fully mouse-driven visits use the same history.
            back(); back(); at(a)
            mouse_open(); at(b); mouse_open(); at(c)
            back(); at(b); forward(); at(c)
            # Plain Left creates a parent visit, rather than moving history back.
            t.send('\x1bOD'); at(b)
            t.send(']'); at(b); assert 'No forward history' in t.screen.row(h-2)
            t.send('\x1bOB\x1bOC'); at(b/'D')
            t.send('['); at(b)
            (b/'D').rmdir()
            t.send(']'); at(b)
            (b/'D').mkdir(); forward(); at(b/'D') # failed move retains forward entry
            # A genuine Back followed by a new visit trims the old branch.
            t.send('[\x1bOH\x1bOC'); at(c)
            t.send(']'); at(c); assert 'No forward history' in t.screen.row(h-2)
            # Dual mode: keyboard acts on active Right; clicking Left activates it.
            t.send('\x1b[20~\x1bOH'+'\x1bOB'*18+'\n')
            t.send('\t\x7f')
            if w<80: at(b)
            t.send('[')
            t.send('\x1b[1;3C')
            if w<80: at(b)
            else: assert ''.join(t.screen.rows[1][w//2:]).rstrip().endswith(str(b))
            assert 'Right* ' in t.screen.row(h-2)
            if w>=80:
                left=''.join(t.screen.rows[1][:w//2]); assert str(c) in left,left
                t.click(2,1); assert 'Left* ' in t.screen.row(h-2)
                assert str(b) in ''.join(t.screen.rows[1][:w//2])
                t.send('\x1b[1;3C'); assert str(c) in ''.join(t.screen.rows[1][:w//2])
                assert str(b) in ''.join(t.screen.rows[1][w//2:])
        finally: t.close()
print('PASS: keyboard/mouse/mixed history, bracket/Alt CSI mappings, plain arrows, empty forward, branch trim, deleted path retry and independent dual histories')

# After an image query the retained raw escape parser owns input (rather than ncurses).
class QueryScreen(Screen):
    queried=False
    def csi(self,params,command):
        if command=='c': self.queried=True; return
        super().csi(params,command)
with tempfile.TemporaryDirectory() as directory:
    a=Path(directory)/'A'; b=a/'B'; c=b/'C'; c.mkdir(parents=True)
    (c/'image.png').write_bytes(b'\x89PNG\r\n\x1a\n')
    t=Terminal(str(a),100,24)
    try:
        screen=QueryScreen(24,100); screen.__dict__.update(t.screen.__dict__); t.screen=screen
        t.send('\x1bOC\x1bOC\n') # file Enter opens image preview and sends query
        assert t.screen.queried
        for kb,kf in [('[',']'),('\x1b[1;3D','\x1b[1;3C')]:
            t.send(kb); assert str(b) in t.screen.row(1) and str(c) not in t.screen.row(1),t.screen.row(1)
            t.send(kf); assert str(c) in t.screen.row(1)
            t.send(kb); t.click(t.screen.row(1).index('[Forward >]')+3 if '[Forward >]' in t.screen.row(1) else 6,1)
            assert str(c) in t.screen.row(1)
    finally: t.close()
print('PASS: bracket/Alt and mixed history after terminal query activates raw escape parsing')
