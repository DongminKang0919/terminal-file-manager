"""Targets, contextual hints and transfer buttons against actual file effects."""
import fcntl
import os
from pathlib import Path
import signal
import struct
import tempfile
import termios
import display_pty
from preview_latency_pty import until,text

display_pty.BINARY=os.path.abspath(os.environ.get('TFILE_BINARY','./tfile'))
F5='\x1b[15~';F6='\x1b[17~';HOME='\x1bOH';DOWN='\x1bOB';END='\x1bOF';BACKTAB='\x1b[Z'
for width,height in [(50,9),(100,24)]:
 for mode in [0,1,2]:
  with tempfile.TemporaryDirectory(prefix='tfile-ux-targets-') as directory:
   base=Path(directory);root=base/'source';root.mkdir();dest=base/'복사';dest.mkdir();moved=base/'이동';moved.mkdir()
   for name in ['a.txt','b-한글.txt','c.txt']:(root/name).write_text(name+'\n'+'preview line\n'*70)
   config=base/'config/tfile';config.mkdir(parents=True);(config/'settings.conf').write_text(f'version=1\nmode={mode}\nimage=0\n')
   os.environ.update(HOME=str(base/'home'),XDG_CONFIG_HOME=str(base/'config'),XDG_DATA_HOME=str(base/'data'),XDG_CACHE_HOME=str(base/'cache'))
   t=display_pty.Terminal(str(root),width,height)
   def body():return text(t)
   def button(label):
    until(t,lambda:label in body())
    for y in range(t.screen.height):
     row=t.screen.row(y)
     if label in row:t.click(row.index(label)+2,y);return
    raise AssertionError(label)
   def replace(value):t.send(HOME+'\x1b[3~'*300+str(value))
   def files_unchanged():assert sorted(p.name for p in root.iterdir())==['a.txt','b-한글.txt','c.txt'] and not list(dest.iterdir())
   try:
    until(t,lambda:'> a.txt' in body())
    assert 'Target: cursor' in t.screen.row(height-3)
    assert 'F5: Copy' in t.screen.row(height-1) and 'F9 Menu' in t.screen.row(0)
    t.send(' '+DOWN)
    assert any(' *a.txt' in t.screen.row(y) for y in range(height))
    assert any('> b-한글.txt' in t.screen.row(y) for y in range(height))
    assert 'Target: 1 marked' in t.screen.row(height-3)
    t.send(' '+DOWN)
    assert 'Target: 2 marked' in t.screen.row(height-3)
    if mode==2:assert 'Left*' in t.screen.row(height-2) and 'Marked: 2' in t.screen.row(height-2)
    t.send(F5)
    assert 'Copy 2 items' in body() and 'From/Base:' in body() and '>To:' in body()
    assert 'Name:' not in body(),'batch must preserve original names'
    t.send('\t\n');until(t,lambda:'Choose destination' in body());t.send('\x1b')
    assert 'To checked' not in body(),'cancelled Browse did not validate To'
    t.send(BACKTAB)
    replace('missing');t.send('\n');until(t,lambda:'Destination:' in body());files_unchanged()
    replace(dest);t.send('\n');until(t,lambda:'Batch copy confirmation' in body())
    assert '[ Copy 2 items ]' in body() and '[ Cancel ]' in body();files_unchanged()
    # Safe default, then retained input and a fresh explicit review request.
    t.send('\n');until(t,lambda:'Batch destination' in body());files_unchanged()
    assert 'To checked' in body()
    t.send('\n');until(t,lambda:'Batch copy confirmation' in body())
    t.send('\t'+BACKTAB+'\n');until(t,lambda:'Batch destination' in body());files_unchanged()
    t.send('\n');until(t,lambda:'Batch copy confirmation' in body())
    button('[ Copy 2 items ]');until(t,lambda:(dest/'b-한글.txt').exists())
    assert (dest/'a.txt').read_text()==(root/'a.txt').read_text() and not (dest/'c.txt').exists()
    until(t,lambda:'Marked: 0' in t.screen.row(height-2))
    assert 'Target: cursor' in t.screen.row(height-3) and '[Success]' in t.screen.row(height-2)
    assert 'F5: Copy' in t.screen.row(height-1)
    # Exactly one marked source, with the cursor deliberately on another file.
    t.send(HOME+' '+END+F6)
    assert 'Move 1 item' in body() and 'From:' in body() and 'a.txt' in body() and 'Name:' in body()
    replace(moved);t.send('\n\n')
    assert not (moved/'a.txt').exists(),'field Enter must not execute'
    button('[ Move 1 item ]');until(t,lambda:(moved/'a.txt').exists())
    assert not (root/'a.txt').exists() and (root/'c.txt').exists() and not (moved/'c.txt').exists()
    until(t,lambda:'Marked: 0' in t.screen.row(height-2))
    # Footer hints describe actual focus and are explanatory, not click targets.
    if mode==1:
     t.send(HOME+'\n');until(t,lambda:'Esc: Files' in t.screen.row(height-1))
     assert 'Up/Down: Row' in t.screen.row(height-1) and 'F5: Copy' not in t.screen.row(height-1)
     t.send(BACKTAB);until(t,lambda:'Space: Mark' in t.screen.row(height-1))
     t.send('\t');until(t,lambda:'Esc: Files' in t.screen.row(height-1))
     t.send(BACKTAB);until(t,lambda:'Space: Mark' in t.screen.row(height-1))
    t.send(F5);assert 'Copy 1 item' in body()
    t.screen=display_pty.Screen(height+1,width+1)
    fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',height+1,width+1,0,0));t.proc.send_signal(signal.SIGWINCH)
    until(t,lambda:'Copy 1 item' not in body())
    assert len(list(moved.iterdir()))==1 and len(list(dest.iterdir()))==2
    print(f'PASS UX targets {width}x{height} mode={mode}: marks/cursor, counts, review/Cancel default, explicit mouse action, Shift+Tab, exact copy/move and resize',flush=True)
   finally:t.close()
