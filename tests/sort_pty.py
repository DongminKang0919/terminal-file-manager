"""Reuse the cell-level VT oracle to check options, identity and scroll policy."""
import os
from pathlib import Path
import tempfile
from display_pty import Terminal

DOWN='\x1bOB'
for width,height in [(100,24),(50,9)]:
    with tempfile.TemporaryDirectory(prefix='tfile-sort-pty-') as directory:
        root=Path(directory)
        for i in range(32):
            path=root/f'e{i:02}'
            path.write_text((f'CONTENT e{i:02}\n')*(100+i))
            os.utime(path,(1700000000+i,1700000000+i))
        t=Terminal(directory,width,height)
        try:
            t.send('\x1bOH'+DOWN*16)
            for _ in range(4): t.send(f'\x1b[<65;{width*3//4};5M')
            def right(): return [t.screen.row(y)[(width*3//5 if width<80 else width*11//20)+1:width-1] for y in range(3,height-2)]
            preview=right()
            def retained(label):
                if width==100:
                    label={'Size+':'Size ascending','Size-':'Size descending','Time-':'Modified descending','Kind-':'Kind descending','Name-':'Name descending'}[label]
                assert label in t.screen.row(2),t.screen.row(2)
                assert 'e16' in '\n'.join(t.screen.row(y)[:(width*3//5 if width<80 else width*11//20)] for y in range(4,height-3))
                assert right()==preview,(right(),preview)
                assert 'Location:' in t.screen.row(1)
            def options(menu=False):
                t.send('\x1b[20~'+DOWN*5+'\n' if menu else '\x1b[18~')
                t.frame((width-min(52,width-4))//2,(height-min(10,height-2))//2,min(52,width-4),min(10,height-2))
            def choose(index,mouse=False):
                if mouse:
                    h=min(10,height-2); x=(width-min(52,width-4))//2; y=(height-h)//2
                    if index>=h-4:
                        t.send(DOWN*index); row=h-4
                    else: row=index+1
                    t.click(x+3,y+row)
                else: t.send(DOWN*index+'\n')
                t.send('\x1b')
            options(menu=True); choose(3)
            retained('Size+')
            options(); choose(4,mouse=True)
            retained('Size-')
            options(); choose(3,mouse=True)
            retained('Time-')
            options(); choose(3)
            retained('Kind-')
            options(); choose(3)
            retained('Name-')
            # Confirm the original target, then use the safe default Cancel.
            t.send('\x1b[19~'); assert 'e16' in '\n'.join(t.screen.row(y) for y in range(height))
            t.send('\n'); retained('Name-')
            (root/'zz-new').write_text('new')
            t.send('r')
            assert 'Refreshed' in t.screen.row(height-2)
            assert 'e16' in t.screen.row(3)[(width*3//5 if width<80 else width*11//20):]  # preview position reset on explicit refresh
            assert 'e16' in '\n'.join(t.screen.row(y)[:(width*3//5 if width<80 else width*11//20)] for y in range(4,height-3))
            if os.geteuid()!=0:
                root.chmod(0)
                try:
                    t.send('r')
                    assert 'Refresh failed' in t.screen.row(height-2)
                    assert 'e16' in '\n'.join(t.screen.row(y)[:(width*3//5 if width<80 else width*11//20)] for y in range(4,height-3))
                    assert ('Name descending' if width==100 else 'Name-') in t.screen.row(2)
                finally: root.chmod(0o700)
            (root/'e16').unlink(); t.send('r')
            assert 'e15' in t.screen.row(3)[(width*3//5 if width<80 else width*11//20):]  # old index, clamped into the new sorted list
            # Main descending order must not leak into the source picker.
            t.send('\x1b[15~\n')
            pw, ph = min(86,width-4), height-2
            px, py = (width-pw)//2, 1
            picker='\n'.join(''.join(t.screen.rows[y][px+1:px+pw-1]) for y in range(py+3,py+ph-3))
            assert 'e00' in picker and 'e31' not in picker,picker
            for i in range(ph-6):
                assert f'e{i:02}' in ''.join(t.screen.rows[py+3+i][px+1:px+pw-1])
            t.send('\x1b\x1b')
        finally: t.close()
        # Settings are session-only.
        t=Terminal(directory,width,height)
        try: assert ('Name ascending' if width==100 else 'Name+') in t.screen.row(2)
        finally: t.close()
print('PASS: Options keyboard/mouse/Menu, 50x9 frames, selected identity/scroll/preview, refresh success/failure/fallback, picker order and restart defaults')
