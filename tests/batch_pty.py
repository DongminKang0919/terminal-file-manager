"""Actual keyboard/mouse selection and batch forms on disposable raw-byte names."""
from pathlib import Path
import os
import tempfile
from display_pty import Terminal
F5='\x1b[15~';F6='\x1b[17~';F8='\x1b[19~';F9='\x1b[20~'
DOWN='\x1bOB';HOME='\x1bOH';END='\x1bOF';PGDN='\x1b[6~'
for w,h in [(50,9),(80,24),(160,32)]:
    with tempfile.TemporaryDirectory(prefix='tfile-batch-pty-') as directory:
        base=Path(directory);root=base/'source';dst=base/'target';moved=base/'moved';root.mkdir();dst.mkdir();moved.mkdir()
        raw=[b'a-'+('한글'*35).encode()+b'\n\xff',b'b\t',b'c',b'.hidden']
        for name in raw:
            with open(os.fsencode(root)+b'/'+name,'wb') as f:f.write(b'body\n'*50)
        t=Terminal(str(root),w,h)
        def body():return '\n'.join(t.screen.row(y) for y in range(h))
        def summary(n):
            text=t.screen.row(h-2)
            assert f'Sel:{n}' in text or f'Selected: {n}' in text or (f'M:{n}' in text and 'S:' in text and 'H:' in text),body()
        def menu(i):t.send(F9+HOME+DOWN*i+'\n')
        def destination(path):t.send('\x1bOH'+'\x1b[3~'*len(str(root))+str(path)+'\n')
        try:
            summary(0);t.send(HOME+' ');summary(1)
            cursor=[r for r in t.screen.rows if r[1]=='>'][0];assert cursor[2]=='*'
            t.send('\t ');summary(1);t.send('\x1b');summary(1)
            t.send('\x06b\x1b');summary(1)
            t.send('r');summary(1)
            # Select all and clear via menu; retain existing menu indexes.
            menu(14);summary(4);menu(15);summary(0)
            t.send(HOME+DOWN+' '+DOWN+' ');summary(2)
            menu(11);assert 'Rename unavailable' in body() or 'multiple marked' in body();summary(2)
            # Finding with a Space is input and must not change the marks.
            t.send('\x06a \x1b');summary(2)
            t.send(F5);assert 'Batch destination' in body();destination(dst)
            assert 'Batch copy confirmation' in body();t.send(END+HOME+PGDN+'\n') # default Cancel after scroll
            summary(2);assert not list(dst.iterdir());t.send('!');assert 'Recent operation result' in body();t.send(END);assert 'Only the latest' in body() or 'details remain' in body();t.send('\x1b')
            # Full copy through keyboard. No Name field in batch form.
            t.send(F5);destination(dst);assert 'Batch copy confirmation' in body();assert 'Name:' not in body();t.send(END+'\t\n');summary(0)
            assert len(list(dst.iterdir()))==2
            # Marked single target is used even when cursor points elsewhere.
            t.send(HOME+DOWN+' '+END+F5);assert 'Batch destination' not in body();t.send('\x1b');summary(1)
            t.send(HOME+DOWN+DOWN+' ');summary(2)
            # Collision stops first target; both marks retained, others unexecuted.
            t.send(F5);destination(dst);t.send('\t\n');summary(2);t.send('!');assert 'Recent operation result' in body();t.send('\x1b')
            # Batch move confirmation executes by mouse. Directory unchanged cursor remains.
            t.send(F6);destination(moved);assert 'Batch move confirmation' in body()
            rw=min(w-8,88);rx=(w-rw)//2;t.click(rx+16,h-3);summary(0);assert len(list(moved.iterdir()))==2
            # Select remaining items, confirm default Cancel and then Delete by mouse.
            menu(14);summary(2);t.send(F8);assert 'Batch delete' in body();t.send(END+HOME+'\n');assert len(list(root.iterdir()))==2
            t.send(F8);t.send(END);t.click(rx+16,h-3);summary(0);assert not list(root.iterdir())
        finally:t.close()
print('PASS: marked/cursor separation, find/input/preview isolation, menu select/clear/rename guard, batch cancel/collision/copy/move/delete/details keyboard+mouse at 50x9/80x24/160x32')
