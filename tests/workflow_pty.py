"""Continuous local workflows on isolated fixtures and real ncurses PTYs."""
import fcntl
import os
from pathlib import Path
import re
import signal
import struct
import tempfile
import re
import termios
from display_pty import Terminal, Screen

HOME='\x1bOH'; DOWN='\x1bOB'; END='\x1bOF'; DELETE='\x1b[3~'; BACK='\x7f'
F2='\x1bOQ'; F3='\x1bOR'; F5='\x1b[15~'; F6='\x1b[17~'; F7='\x1b[18~'; F8='\x1b[19~'; F9='\x1b[20~'
for width,height in [(50,9),(80,24),(160,32)]:
    with tempfile.TemporaryDirectory(prefix='tfile-workflow-') as directory:
        root=Path(directory); a=root/'A'; b=root/'B'; c=root/'C'
        for p in [a,b,c,a/'03-nested',a/'04-empty',a/'05-hidden-only',a/'06-locked']: p.mkdir()
        (a/'01-한글 파일.txt').write_text('body\n'*80)
        (a/'02-space file.txt').write_text('second')
        (a/'.hidden').write_text('hidden');(a/'05-hidden-only'/'.only').write_text('hidden')
        (a/('long 이름 '+ '한'*50)).write_text('long')
        (a/'file-link').symlink_to('01-한글 파일.txt');(a/'directory-link').symlink_to('03-nested',target_is_directory=True)
        (a/'broken-link').symlink_to('missing');(a/'binary').write_bytes(b'\0binary')
        (a/'03-nested'/'leaf-one').write_text('first');(a/'03-nested'/'leaf-two').write_text('second')
        (b/'peer.txt').write_text('peer')
        t=Terminal(str(a),width,height);w,h=width,height
        def body():return '\n'.join(t.screen.row(y) for y in range(h))
        def menu(index):
            marked=re.search(r"Marked: (\d+)",t.screen.row(h-2))
            steps=index-(1 if index>11 and marked and int(marked[1])>1 else 0)
            t.send(F9+HOME+DOWN*steps+'\n')
        def find(name):t.send('\x06'+name+'\n')
        def marks(n):assert f'Marked: {n}' in t.screen.row(h-2),body()
        def side(name):assert f'{name}* ' in t.screen.row(h-2) or f'* {name}' in body(),body()
        def replace(value):t.send(HOME+DELETE*600+str(value))
        def button(label):
            for y in range(h):
                row=t.screen.row(y)
                if label in row:t.click(row.index(label)+2,y);return
            raise AssertionError(body())
        def panel(name):
            x=0 if w<80 or name=='Left' else w//2
            pw=w if w<80 else w//2 if name=='Left' else w-w//2
            return [''.join(t.screen.rows[y][x:x+pw]) for y in range(4 if h<12 else 5,h-2)]
        def details():
            t.send('!');seen={};previous=0;pw=min(78,w-8);px=(w-pw)//2
            while True:
                match=re.search(r'Lines (\d+)-(\d+)/(\d+)',body());assert match,body()
                first,last,total=map(int,match.groups());assert first>previous;previous=first
                for n in range(first,last+1):seen[n]=''.join(t.screen.rows[2+n-first][px+2:px+pw-2]).rstrip()
                if last==total:break
                t.send('\x1b[6~')
            t.send('\x1b');return ''.join(seen[n] for n in range(1,total+1)).replace(' ','')
        def resize(nw,nh):
            t.screen=Screen(nh,nw)
            fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',nh,nw,0,0))
            os.kill(t.proc.pid,signal.SIGWINCH);t.read()
        try:
            menu(18);t.send('\t'+BACK+HOME+DOWN+'\n');find('peer');t.send(' ');marks(1)
            if w>=80:t.click(2,2)
            else:t.send('\t')
            side('Left');find('.hidden');t.send(' ');marks(1)
            t.send(F7+DOWN*3+'\n'+DOWN+'\n\x1b');marks(1)
            assert '.hidden' in ''.join(panel('Left'))
            t.send(F7+'\n\x1b');marks(0) # filter removes the now-invisible mark
            t.send(F3+'leaf-one\n');t.send('\n');side('Left');assert '03-nested' in body()
            find('leaf-one');t.send(' ');find('leaf-two');t.send(' ');marks(2)
            left=panel('Left');t.send(F5);assert str(b) in body() or '/B' in body()
            t.send('\n');assert not (b/'leaf-one').exists();t.send('\t\n')
            assert (b/'leaf-one').read_text()=='first' and (b/'leaf-two').read_text()=='second'
            marks(0)
            expected=[s.replace('*',' ',1) if '*' in s[1:3] else s for s in left[:-1]]
            # Successful copy clears marks and updates the independent target caption.
            # Retain an exact comparison of all list rows and every border cell.
            caption='└─ Target: cursor '
            expected.append(caption+'─'*(len(left[-1])-len(caption)-1)+'┘')
            assert panel('Left')==expected, body()
            t.send('\t');side('Right');marks(1);t.send('\t')
            # Real middle collision in current Size-descending order: successes
            # unmark, failed/unexecuted retain marks, and the peer mark survives.
            for name,n in [('a-partial',400),('b-collision',200),('c-unexecuted',100)]:
                (a/'03-nested'/name).write_text('x'*n)
            (b/'b-collision').write_text('keep collision')
            t.send('r')
            for name in ['a-partial','b-collision','c-unexecuted']:find(name);t.send(' ')
            marks(3);t.send(F5+'\n');t.send('\t\n');assert 'Name collision' in body();t.send('\n');marks(2)
            assert (b/'a-partial').read_text()=='x'*400 and (b/'b-collision').read_text()=='keep collision' and not (b/'c-unexecuted').exists()
            assert '[Partial]' in body();result=details()
            for expected in ['Partialcompletion','Target1/3:Success','Target2/3:Failed','Target3/3:Unexecuted']:assert expected in result,result
            t.send('\t');marks(1);t.send('\t');menu(15)
            # Cursor-only copy; edit relative to nested source, leave B untouched.
            find('leaf-one');t.send(F5);replace('../../C');button('[ Copy now ]')
            assert (c/'leaf-one').read_text()=='first';t.send('\t');marks(1);assert '/B' in body();t.send('\t')
            # Browse to a third destination and retain Name edit/explicit run.
            find('leaf-two');t.send(F6+'\t\t\n');t.send('p');replace(c);t.send('\n ')
            button('[ Move now ]');assert not (a/'03-nested'/'leaf-two').exists() and (c/'leaf-two').exists()
            t.send('!'+END+'\x1b');side('Left');t.send(BACK);find('01-한글');t.send(END)
            saved=panel('Left');t.send(BACK);find('C');t.send('\n');t.send('[[');assert panel('Left')==saved
            t.send(']]');assert '/C' in body();t.send('[[');assert panel('Left')==saved
            find('02-space');menu(11);t.send(HOME+'renamed-\n');assert (a/'renamed-02-space file.txt').exists()
            t.send(F2+'new file 한글\n');assert (a/'new file 한글').exists()
            t.send(F8+'\t\n');assert not (a/'new file 한글').exists()
            # Explicit modes clear only the hidden peer's marks. No past peer mark
            # may later become a source target. Temporary narrow resize keeps marks.
            find('01-한글');t.send(' ');marks(1);menu(16);t.send('\t'+DOWN+'\x1b');menu(17);menu(18)
            side('Left');marks(1);t.send('\t');marks(0);t.send('\t');menu(15)
            find('04-empty');t.send('\n');assert 'No visible items' in body();t.send(BACK)
            find('05-hidden-only');t.send('\n');assert 'No visible items' in body()
            t.send(F7+'\n\x1b');assert '.only' in body();t.send(BACK)
            menu(16)
            for name in ['file-link','broken-link','binary']:
                find(name);t.send('\n'+DOWN*4+'\x1b');assert 'Preview' in body()
            menu(18);find('01-한글');t.send(' ');t.send(F5);resize(52,10);w,h=52,10
            assert 'Copy now' not in body();marks(1);resize(width,height);w,h=width,height;marks(1)
            # Warm up then observe repeated forms/help/details: stable FDs and
            # bounded RSS are observations, not proof of whole-program leak absence.
            for _ in range(5):t.send(F5+'\x1b'+'\x1bOP\x1b'+'!\x1b')
            def usage():
                fd=len(list(Path(f'/proc/{t.proc.pid}/fd').iterdir()))
                rss=int(re.search(r'VmRSS:\s+(\d+)',Path(f'/proc/{t.proc.pid}/status').read_text()).group(1))
                return fd,rss
            samples=[usage()]
            for _ in range(20):t.send(F5+'\x1b'+'\x1bOP\x1b'+'!\x1b');samples.append(usage())
            assert len({fd for fd,_ in samples})==1,samples
            print(f'PASS workflow {width}x{height}: normal chained operations/modes/history; FD={samples[0][0]}, RSS={min(r for _,r in samples)}..{max(r for _,r in samples)} KiB',flush=True)
        finally:t.close()
