"""Conflict choices freeze targets and preserve skipped files/marks in a PTY."""
import fcntl,os,signal,struct,termios,tempfile
from pathlib import Path
from display_pty import Terminal
for action in ['copy','move']:
    for choice in ['skip','all','stop','cancel','resize']:
        with tempfile.TemporaryDirectory(prefix='tfile-conflict-pty-') as d:
            src=Path(d)/'source';dst=Path(d)/'target';src.mkdir();dst.mkdir()
            for name in ['a','b','c']: (src/name).write_text('original')
            (dst/'a').write_text('existing');(dst/'b').write_text('existing')
            t=Terminal(str(src))
            def text():return '\n'.join(t.screen.row(y) for y in range(t.screen.height))
            try:
                t.send('\x1b[20~'+'\x1bOB'*14+'\n') # select all visible
                t.send(('\x1b[15~' if action=='copy' else '\x1b[17~')+'\x1bOH'+'\x1b[3~'*200+str(dst)+'\t\t\n')
                t.send('\t\n');assert 'Name collision' in text(),text()
                if choice in ['skip','all']:
                    t.send('\x1bOB'*(1 if choice=='skip' else 2)+'\n')
                    if choice=='skip': assert 'Name collision' in text();t.send('\x1bOB\n')
                    assert (dst/'c').read_text()=='original'
                    assert (src/'c').exists()==(action=='copy')
                    t.send('!');assert 'Skipped name collisions: 2' in text();t.send('\x1b')
                    assert 'Marked: 2' in text()
                elif choice=='stop': t.send('\n');assert not (dst/'c').exists()
                elif choice=='cancel': t.send('\x1b');assert not (dst/'c').exists()
                else:
                    fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',30,120,0,0))
                    t.screen.width=120;t.screen.height=30;t.screen.rows=[[' ']*120 for _ in range(30)]
                    t.screen.scroll_bottom=29;t.screen.x=t.screen.y=0
                    os.kill(t.proc.pid,signal.SIGWINCH);t.read();assert not (dst/'c').exists()
                assert (dst/'a').read_text()=='existing' and (dst/'b').read_text()=='existing'
                assert (src/'a').read_text()=='original' and (src/'b').read_text()=='original'
            finally:t.close()
print('PASS: batch conflict skip/skip-all/stop/Esc/resize, exact file effects and marks for copy/move (PTY)')
