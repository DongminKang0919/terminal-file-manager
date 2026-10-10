"""Read every wrapped help line through the real PTY without entering footer/title rows."""
import fcntl
import os
from pathlib import Path
import re
import signal
import struct
import tempfile
import termios
from display_pty import Terminal, Screen

for width,height in [(50,9),(80,24),(160,32)]:
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory)
        (root/'a-한글\nlong-name').write_text('selected preview\n'*80)
        (root/'b-second').write_text('second')
        t=Terminal(directory,width,height)
        pw,ph=min(78,width-8),height-2
        px,py=(width-pw)//2,1
        try:
            for _ in range(3): t.send(f'\x1b[<65;{width-3};5M')
            before=[t.screen.row(y) for y in range(height)]
            t.send('\x1bOP')
            seen={}; previous=0
            # Bound traversal by the reported line count and strict forward progress.
            while True:
                t.frame(px,py,pw,ph)
                assert 'Help' in t.screen.row(py)
                footer=''.join(t.screen.rows[py+ph-3][px+2:px+pw-2])
                match=re.search(r'Lines (\d+)-(\d+)/(\d+) \| (Top|\^ more) (v more|End)',footer)
                assert match,footer
                start,end,total=map(int,match.group(1,2,3))
                assert end-start+1<=ph-4 and 1<=start<=end<=total and start>previous
                assert 'Wheel/Arrows/PgUp/PgDn  Esc: close' in t.screen.row(py+ph-2)
                for number in range(start,end+1):
                    text=''.join(t.screen.rows[py+1+number-start][px+2:px+pw-2]).rstrip()
                    assert '...' not in text,text
                    if number in seen: assert seen[number]==text,(width,number,seen[number],text)
                    seen[number]=text
                if end==total: break
                previous=start; t.send('\x1b[6~')
            assert set(seen)==set(range(1,total+1))
            body=' '.join(' '.join(seen[i].split()) for i in range(1,total+1))
            for text in ['Plain Right opens the selected directory', 'A new directory visit clears forward history',
                         'terminal intercepts Alt+arrows', 'GETTING STARTED','NAVIGATION','FILE OPERATIONS','SEARCH','PREVIEW','OPTIONS',
                         'never overwritten','Cancel is selected by default','cannot be undone',
                         'incomplete copies are kept','deleted items are not restored',
                         'Cancel/Esc/Enter keeps collected results','x or resize stops and closes',
                         'Sorting preserves','Save startup defaults explicitly in Options;',
                         'sort/hidden defaults come from the active panel.',
                         'Popup input cannot operate the background', 'SCREEN MODES AND FILE PANELS',
                         'Left + Right files', 'STALE lists block file changes', 'both refresh results separately']:
                assert text in body,text
            t.send('\x1bOH'); assert 'Lines 1-' in t.screen.row(py+ph-3)
            t.send(f'\x1b[<65;{px+4};{py+3}M'); assert 'Lines 2-' in t.screen.row(py+ph-3)
            t.send('\x1bOQ\x1b[15~\x1b[18~')  # New/Copy/Options are blocked by Help
            t.frame(px,py,pw,ph)
            t.click(px+pw-4,py)
            assert [t.screen.row(y) for y in range(height)]==before
            assert len(list(root.iterdir()))==2
            t.send('\x1bOP\x1bOF')
            assert ' End' in t.screen.row(py+ph-3)
            t.send('\x1b'); assert [t.screen.row(y) for y in range(height)]==before
            t.send('\x1bOP')
            t.screen=Screen(10,52)
            fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',10,52,0,0))
            os.kill(t.proc.pid,signal.SIGWINCH);t.read()
            assert 'Enter: Open' in t.screen.row(9)
            assert 'Help' not in '\n'.join(t.screen.row(y) for y in range(2,8))
        finally:t.close()
print('PASS: complete wrapped Help at 50x9/80x24/160x32, explicit display-line ranges, both scroll boundaries, footer guards, blocked background commands, exact restore and resize')
