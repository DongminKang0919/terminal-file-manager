"""Cell-level checks of the main layout at supported sizes and terminal palettes."""
import os
from pathlib import Path
import tempfile
from display_pty import Terminal

for width, height in [(50,9),(80,24),(100,24),(160,32)]:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        (root/'a-dir').mkdir()
        (root/'b-short').write_text('BODY\n')
        (root/'c-long').write_text(''.join(f'LINE {i:03}\n' for i in range(70)))
        (root/'d-binary').write_bytes(b'\x00\x01binary')
        (root/'e-link').symlink_to('b-short')
        (root/'f-error').write_text('private'); (root/'f-error').chmod(0)
        split = width*3//5 if width<80 else width*11//20
        first = 4 if height<12 else 5
        t = Terminal(directory,width,height)
        try:
            t.frame(0,2,split,height-4); t.frame(split,2,width-split,height-4)
            assert '[>]  Location:' in t.screen.row(1)
            assert t.screen.rows[first][1]=='>'
            assert 'a-dir' in t.screen.row(first) and 'Dir' in t.screen.row(first)
            if split>=48: assert t.screen.rows[first][split-21 if split>=72 else split-4]=='-'
            if split>=72:
                assert 'Modified' in t.screen.row(4)
                assert t.screen.row(first)[split-18:split-14].isdigit()
            t.click(4,first+1)
            assert t.screen.rows[first+1][1]=='>'
            t.click(split,first)
            assert t.screen.rows[first+1][1]=='>'  # border must not select
            if height>=12:
                t.click(4,4)
                assert t.screen.rows[first+1][1]=='>'  # column header is not a row
            t.send('\x1bOH\x1bOB')
            if height>=12:
                assert 'BODY' in t.screen.row(6)[split:]
                assert 'Row' not in t.screen.row(height-4)[split:]
                assert str(root) not in '\n'.join(t.screen.row(y)[split:] for y in range(3,height-3))
            t.send('\x1bOB')
            t.send(f'\x1b[<65;{width-3};5M' * 90)
            assert 'End' in t.screen.row(height-4)[split:]
            assert 'LINE 069' in '\n'.join(t.screen.row(y)[split:] for y in range(3,height-4))
            t.send('\x1bOB')
            if height>=12: assert 'Binary file' in t.screen.row(6)[split:]
            t.send('\x1bOB')
            if height>=12: assert 'Link target:' in t.screen.row(6)[split:]
            t.send('\x1bOB')
            if os.geteuid()!=0 and height>=12:
                assert 'Error:' in t.screen.row(4)[split:]
        finally: t.close(); (root/'f-error').chmod(0o600)
    with tempfile.TemporaryDirectory() as directory:
        t=Terminal(directory,width,height)
        try:
            assert 'Empty directory' in t.screen.row(first)
            assert 'Row' not in '\n'.join(t.screen.row(y) for y in range(height))
        finally: t.close()
print('PASS: main panel cells at 50x9/80x24/100x24/160x32, selection, columns, empty list, short/EOF/binary/link/error previews')
