"""Real installed Vim in a controlled PTY. Files/config only in temporary roots.
Automated process/termios/file/screen checks, not a human terminal inspection.
"""
import fcntl,json,os,shutil,signal,struct,tempfile,termios,time
from pathlib import Path
from display_pty import Terminal,Screen
from vim_tools import VIM_STUB

class VimScreen(Screen):
 def __init__(self,*args):
  super().__init__(*args);self.mouse_modes={};self.modify_other_keys=0
 def csi(self,params,command):
  if params.startswith(">4;") and command=="m":self.modify_other_keys=int(params.split(";")[-1] or 0)
  if params.startswith("?") and command in ("h","l"):
   for mode in params[1:].split(";"):
    if mode.isdigit():self.mouse_modes[int(mode)]=command=="h"
  # Ignore device queries, keyboard reporting and cursor shape, which do not
  # paint cells. Keep the base screen's strict painting checks unchanged.
  if params.startswith(('>','<')) or command in ('n','c','m') or (command=='q' and params.endswith(' ')) or (command=='p' and params=='0%'):return
  try:super().csi(params,command)
  except ValueError as e:raise AssertionError((params,command)) from e

def text(t):return '\n'.join(t.screen.row(y) for y in range(t.screen.height))
def wait(t,test):
 end=time.monotonic()+5
 while not test():
  assert time.monotonic()<end,text(t)
  t.read()
def children(t):
 return [int(x) for x in Path(f'/proc/{t.proc.pid}/task/{t.proc.pid}/children').read_text().split()]
def editor(t):
 wait(t,lambda:bool(children(t)))
 pid=children(t)[0]
 wait(t,lambda:Path(f'/proc/{pid}/comm').read_text().strip().startswith('vim'))
 t.read();return pid

def restored(t):
 wait(t,lambda:not children(t) and 'Space: Mark' in text(t))
 assert 'e: Vim' in text(t) and 'Marked: 1' in text(t)
 assert t.screen.mouse_modes.get(1000) and t.screen.mouse_modes.get(1006),t.screen.mouse_modes
 assert not any(t.screen.mouse_modes.get(n) for n in [2004,1004,1002,1003]),t.screen.mouse_modes
 assert not t.screen.modify_other_keys,t.screen.modify_other_keys
 flags=termios.tcgetattr(t.master)[3]
 assert not flags&(termios.ICANON|termios.ECHO)
 # A menu immediately after return must not consume a leftover resize event.
 t.send('\x1b[20~');assert 'Menu' in text(t);t.send('\x1b')

vim=shutil.which('vim')
assert vim,'check-vim real PTY requires installed Vim; install it manually, no automatic install'
with tempfile.TemporaryDirectory(prefix='tfile-vim-real-pty-') as d:
 base=Path(d);files=base/'files';files.mkdir();home=base/'home';home.mkdir()
 (home/'.vimrc').write_text('set nocompatible noswapfile shortmess+=I mouse=a\n')
 target=files/'- 한글 ; $(touch marker)';target.write_text('original\n')
 marked=files/'z marked';marked.write_text('marked\n')
 env_keys=['HOME','XDG_CONFIG_HOME','VISUAL','EDITOR','VIMINIT','EXINIT']
 old={k:os.environ.get(k) for k in env_keys}
 os.environ.update(HOME=str(home),XDG_CONFIG_HOME=str(base/'config'),VISUAL='not-an-editor',EDITOR='not-an-editor')
 os.environ.pop('VIMINIT',None);os.environ.pop('EXINIT',None)
 t=Terminal(str(files));t.screen=VimScreen(24,100);t.send("r")
 try:
  t.send('\x1bOB \x1bOH'+'e');pid=editor(t)
  args=Path(f'/proc/{pid}/cmdline').read_bytes().split(b'\0');assert args[0]==b'vim' and args[1]==b'--' and args[2]==os.fsencode(target)
  fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',30,120,0,0));t.screen=VimScreen(30,120)
  os.kill(t.proc.pid,signal.SIGWINCH);os.kill(pid,signal.SIGWINCH);t.read()
  t.send('gg0iEdited 한글\n\x1b:wq\r');restored(t)
  assert target.read_text().startswith('Edited 한글\n') and marked.read_text()=='marked\n'
  assert not (files/'marker').exists() and 'Edited 한글' in text(t)
  # Mouse reporting and file focus work after Vim enabled its own mouse mode.
  row=next(i for i in range(30) if 'z marked' in t.screen.row(i));t.click(8,row)
  assert any('>*z marked' in t.screen.row(i) for i in range(30)),text(t)
  t.send('\x1bOH');t.send('\x1b[20~'+'\x1bOB'*21+'\n');pid=editor(t)
  t.send(':q!\r');restored(t)  # F9 normal return without a save
  t.send('e');pid=editor(t);os.kill(pid,signal.SIGKILL);restored(t)
  assert 'signal 9' in text(t),text(t)
  # Explicit -R is visible in a real Vim invocation; do not write the fixture.
  target.chmod(0o444);t.send('e');pid=editor(t)
  assert b'-R' in Path(f'/proc/{pid}/cmdline').read_bytes().split(b'\0')
  t.send(':q!\r');restored(t);assert 'read-only' in text(t)
  # Execution failure and binary rejection leave a usable screen, focus and marks.
 finally:
  for pid in children(t):
   try:os.kill(pid,signal.SIGKILL)
   except ProcessLookupError:pass
  t.read();t.close()
  for k,v in old.items():
   if v is None:os.environ.pop(k,None)
   else:os.environ[k]=v
 print('PASS: real installed Vim edit/write/quit, hostile cursor path vs marks, -R, signal termination, resize, ncurses/input/mouse/preview restoration (automated PTY)')

with tempfile.TemporaryDirectory(prefix='tfile-vim-failure-pty-') as d:
 base=Path(d);files=base/'files';files.mkdir();file=files/'config';file.write_text('safe')
 config=base/'config/tfile';config.mkdir(parents=True);(config/'settings.conf').write_text('version=1\nimage=0\n')
 tools=base/'tools';tools.mkdir();log=base/'log';os.environ.update(TFILE_VIM_LOG=str(log),XDG_CONFIG_HOME=str(base/'config'))
 old_path=os.environ['PATH'];os.environ['PATH']=str(tools)
 for mode in ['missing','exec-failure','exit','binary','link']:
  if mode=='exec-failure':(tools/'vim').write_text('not a valid executable');(tools/'vim').chmod(0o700)
  if mode=='exit':(tools/'vim').write_text(VIM_STUB);(tools/'vim').chmod(0o700);os.environ['TFILE_VIM_MODE']='fail'
  if mode=='binary':file.write_bytes(b'%PDF-1.7\n')
  if mode=='link':file.unlink();file.symlink_to('/does-not-exist')
  t=Terminal(str(files))
  try:
   t.send('e')
   expected={'missing':'install vim','exec-failure':'External exec failed','exit':'status 9','binary':'binary/media','link':'regular file'}[mode]
   assert expected in text(t),text(t)
   t.send('\x1b[20~');assert 'Menu' in text(t);t.send('\x1b')
  finally:t.close()
 os.environ['PATH']=old_path;os.environ.pop('TFILE_VIM_MODE',None)
print('PASS: missing Vim, ENOEXEC/no shell fallback, failed exit, binary/link refusal and immediate menu restoration (PTY)')
