"""Panel keyboard focus, in-place Options and recoverable generic form errors."""
import fcntl
import os
from pathlib import Path
import re
import signal
import struct
import tempfile
import termios
from display_pty import Terminal, Screen

DOWN='\x1bOB'; UP='\x1bOA'; HOME='\x1bOH'; END='\x1bOF'; PGDN='\x1b[6~'; PGUP='\x1b[5~'
for w,h in [(50,9),(80,24),(160,32)]:
    split=w*3//5 if w<80 else w*11//20
    first=4 if h<12 else 5
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory)
        (root/'z-dir').mkdir()
        (root/'00-한글-text').write_text(''.join(f'LINE {i:03}\n' for i in range(100)))
        (root/'01-other').write_text('other')
        (root/'02-binary').write_bytes(b'\0binary')
        (root/'03-link').symlink_to('00-한글-text')
        (root/'04-dirlink').symlink_to('z-dir')
        (root/'05-error').write_text('private'); (root/'05-error').chmod(0)
        for i in range(35): (root/f'zz{i:02}').write_text('tail')
        t=Terminal(directory,w,h)
        def focus(preview):
            assert t.screen.rows[2][split+2 if preview else 2]=='*'
            assert t.screen.rows[2][2 if preview else split+2]==' '
            assert ('Esc: Files' if preview else 'Enter: Open') in t.screen.row(h-1)
        def row():
            match=re.search(r'Row (\d+)', ''.join(t.screen.rows[h-4][split:]))
            assert match,t.screen.row(h-4)
            return int(match[1])
        def left(): return [''.join(t.screen.rows[y][:split]) for y in range(first,h-2)]
        try:
            t.send('\n')
            assert 'z-dir' in t.screen.row(1); focus(False)
            t.send('\x7f'+DOWN)
            selected=left()
            t.send('\n'); focus(True); assert left()==selected
            assert row()==1
            t.send(DOWN); assert row()==2 and left()==selected
            t.send(PGDN); assert row()==2+h-7 and left()==selected
            offset=row(); preview=[''.join(t.screen.rows[y][split:]) for y in range(3,h-2)]
            t.send(END+'\x1bOC\x1bOD\x7f\n')
            assert row()==offset and left()==selected # no list or parent navigation
            t.send('\t'); focus(False)
            t.send('\x1b[Z'); focus(True)
            assert row()==offset
            t.send('\x1bOP\x1b'); focus(True)
            assert [''.join(t.screen.rows[y][split:]) for y in range(3,h-2)]==preview
            t.send('\x1b[15~\x1b'); focus(True) # F5 target remains selected
            assert left()==selected and row()==offset
            t.send('\x1b'); focus(False)
            t.send('\n'); focus(True); assert row()==offset # Enter preserves cached viewport
            t.send(PGUP); assert row()==2
            t.send(HOME); assert row()==1
            for _ in range(60):
                if 'End' in t.screen.row(h-4)[split:]: break
                t.send(PGDN)
            else: raise AssertionError('preview did not reach EOF')
            assert 'LINE 099' in '\n'.join(t.screen.row(y)[split:] for y in range(3,h-3))
            assert left()==selected
            t.send('\x1b'); focus(False)
            # Clicking each panel changes focus, without selecting an item in preview.
            t.click(split+3,3); focus(True)
            t.click(20,3); focus(False)
            # Hide preview from its own focus, then Open must turn it back on.
            t.send('\t\x1b[18~'+DOWN+'\n\x1b')
            t.frame(0,2,w,h-4); assert t.screen.rows[2][2]=='*'
            t.send('\t\x1b[Z'); assert 'Preview' not in t.screen.row(2)
            t.send('\n'); focus(True); assert row()==1
            # Same Options window keeps both focus and scroll on repeated changes.
            t.send('\x1b[18~'+DOWN*2+'\n')
            assert 'Wheel scroll: 3' in '\n'.join(t.screen.row(y) for y in range(h))
            t.send('\n'); assert 'Wheel scroll: 5' in '\n'.join(t.screen.row(y) for y in range(h))
            oh=min(10,h-2); oy=(h-oh)//2; ox=(w-min(52,w-4))//2
            t.click(ox+3,oy+3) # still the wheel row at both sizes
            assert 'Wheel scroll: 1' in '\n'.join(t.screen.row(y) for y in range(h))
            t.send(DOWN+'\n\n')
            assert 'Sort by: Modified' in '\n'.join(t.screen.row(y) for y in range(h))
            t.send('\x1b'); focus(True)
            # Return sort to name before testing each non-text state.
            t.send('\x1b[18~'+DOWN*3+'\n\n\x1b\x1b')
            for index,expected in [(3,'Binary file'),(4,'Link target:'),(6,'Error:')]:
                if index==6 and os.geteuid()==0: continue
                t.send(HOME+DOWN*index+'\n'); focus(True)
                collected=[]
                for _ in range(5):
                    collected.extend(t.screen.row(y)[split:] for y in range(3,h-3))
                    t.send(DOWN)
                assert expected in '\n'.join(collected),(expected,collected)
                t.send('\x1b'); focus(False)
            t.send(HOME+DOWN*5+'\n')
            assert 'z-dir' in t.screen.row(1); focus(False)
            t.send('\x7f'+HOME+DOWN+'\n'); focus(True)
            # Resizing closes a popup and restores the valid preview focus.
            t.send('\x1bOP')
            t.screen=Screen(10,52)
            fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',10,52,0,0))
            os.kill(t.proc.pid,signal.SIGWINCH); t.read()
            assert 'Esc: Files' in t.screen.row(9)
            assert t.screen.rows[2][52*3//5+2]=='*'
        finally: t.close(); (root/'05-error').chmod(0o600)
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory); (root/'.only-hidden').write_text('hidden')
        t=Terminal(directory,w,h)
        try:
            t.send('\x1b[18~\n\x1b')
            assert 'No visible items' in t.screen.row(first)
            assert 'F7: Show hidden files' in t.screen.row(first+1)
            assert 'Shown: 0 | Hidden: off' in t.screen.row(h-2)
            t.send('\t'+DOWN+PGDN+HOME+'\x1b') # empty preview is navigable and harmless
            assert 'No visible items' in t.screen.row(first)
        finally: t.close()
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory); (root/'source').write_text('content')
        t=Terminal(directory,w,h)
        try:
            t.send('\x1b[17~\t\t\n') # move -> name
            ph=min(9,h-2); py=(h-ph)//2
            t.send(HOME+'\x1b[3~'*100+'\n')
            assert 'Enter a value to continue.' in t.screen.row(py+4)
            assert '[ OK ]' in t.screen.row(py+ph-2)
            t.send('한글-renamed\n\t\t\n')
            assert (root/'한글-renamed').read_text()=='content' and not (root/'source').exists()
            t.send('\x1b[15~\t\np') # copy -> destination -> Enter path
            t.send(HOME+'\x1b[3~'*200+'\n')
            assert 'Enter a value to continue.' in t.screen.row(py+4)
            t.send(directory+'\n ')
            assert 'Copy' in '\n'.join(t.screen.row(y) for y in range(h))
            t.send('\x1b')
        finally: t.close()
print('PASS: keyboard preview pages/EOF/Home/Esc, Tab/click focus, Enter cache viewport, hidden preview, directory/link/binary/error/empty states, retained Options keyboard/mouse focus, recoverable name/path input and resize at 50x9/80x24/160x32')
