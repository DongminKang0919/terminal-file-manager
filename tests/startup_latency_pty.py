"""Event-gated initial path/list reads; no real HOME or user files are used."""
import fcntl
import os
from pathlib import Path
import pty
import select
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time
import display_pty

BASELINE='--baseline' in sys.argv
BINARY=os.path.abspath(os.environ.get('TFILE_STARTUP_BINARY',os.environ.get('TFILE_IO_BINARY','tests/tfile_ui_io')))

class Session:
 def __init__(self,root,args,env,size):
  self.master,self.slave=pty.openpty();self.before=termios.tcgetattr(self.slave)
  self.screen=display_pty.Screen(size[1],size[0]);self.raw=bytearray();self.resize(size)
  self.started=time.monotonic()
  self.proc=subprocess.Popen([BINARY]+args,cwd=root,env={**env,'TERM':'xterm-256color','LC_ALL':'C.UTF-8'},stdin=self.slave,stdout=self.slave,stderr=self.slave)
 def resize(self,size):
  if (self.screen.width,self.screen.height)!=size:self.screen=display_pty.Screen(size[1],size[0])
  fcntl.ioctl(self.slave,termios.TIOCSWINSZ,struct.pack('HHHH',size[1],size[0],0,0))
  if hasattr(self,'proc'):self.proc.send_signal(signal.SIGWINCH)
 def pump(self,wait=.01):
  if select.select([self.master],[],[],wait)[0]:
   try:data=os.read(self.master,65536)
   except OSError:return
   self.raw.extend(data);self.screen.feed(data)
 def text(self):return '\n'.join(self.screen.row(i) for i in range(self.screen.height))
 def until(self,predicate,timeout=3):
  deadline=time.monotonic()+timeout
  while not predicate():
   assert time.monotonic()<deadline,(self.proc.poll(),self.text(),bytes(self.raw[-2000:]))
   self.pump()
 def send(self,data):os.write(self.master,data)
 def finish(self,code):
  self.until(lambda:self.proc.poll() is not None)
  while select.select([self.master],[],[],0)[0]:self.pump(0)
  assert self.proc.returncode==code,(self.proc.returncode,self.raw)
  assert termios.tcgetattr(self.slave)==self.before,'terminal attributes not restored'
  assert b'\x1b[?1006;1000l' in self.raw or b'\x1b[?1000l' in self.raw,'mouse not disabled'
  assert b'\x1b[?25h' in self.raw,'cursor not restored'
 def close(self):
  if self.proc.poll() is None:self.proc.kill();self.proc.wait()
  os.close(self.master);os.close(self.slave)

def fixture(base,mode):
 root=base/'root';root.mkdir();(root/'a.txt').write_text('startup payload\n');(root/'z.txt').write_text('last\n');(root/'.hidden').write_text('hidden\n')
 config=base/'config/tfile';config.mkdir(parents=True);(config/'settings.conf').write_text(f'version=1\nmode={mode}\nhidden=0\nimage=0\n')
 env={**os.environ,'HOME':str(base/'home'),'XDG_CONFIG_HOME':str(base/'config'),'XDG_DATA_HOME':str(base/'data'),'XDG_CACHE_HOME':str(base/'cache')}
 for key in ['TFILE_IO_PHASE','TFILE_IO_GATE','TFILE_IO_MATCH','TFILE_IO_SKIP']:env.pop(key,None)
 return root,env

def release(gate):
 gate.unlink(missing_ok=True)
 try:fd=os.open(str(gate)+'.release',os.O_WRONLY|os.O_NONBLOCK)
 except OSError:return
 os.write(fd,b'x');os.close(fd)

def gated(size,mode,phase,action,implicit=False,second=False):
 with tempfile.TemporaryDirectory(prefix='tfile-startup-gate-') as tmp:
  base=Path(tmp);root,env=fixture(base,mode);gate=base/'gate';gate.touch();os.mkfifo(str(gate)+'.release');marker=base/'gate.entered'
  env.update(TFILE_IO_PHASE=phase,TFILE_IO_GATE=str(gate),TFILE_IO_MATCH='.' if implicit else str(root))
  # Second-panel gate uses absolute resolved path while first request is relative.
  if second:env['TFILE_IO_MATCH']=str(root);args=['.'];env.pop('TFILE_IO_SKIP',None)
  else:args=[] if implicit else [str(root)]
  t=Session(root,args,env,size)
  try:
   t.until(lambda:marker.exists() and bool(marker.read_text().strip()))
   pid=int(marker.read_text().split()[0]);gate.unlink()
   if BASELINE:
    assert pid==t.proc.pid
    sent=time.monotonic()
    if action=='resize':t.resize((size[0]+1,size[1]+1))
    else:t.send(b'\x1b' if action=='esc' else b'\x1b[21~')
    deadline=sent+.2
    while time.monotonic()<deadline:t.pump()
    assert t.proc.poll() is None and b'\x1b[?1049h' not in t.raw
    print(f'BEFORE {phase} mode={mode} {size} {action}: no first frame, UI pid={pid}, blocked >= {(time.monotonic()-sent)*1000:.1f}ms',flush=True)
    release(gate);t.until(lambda:'a.txt' in t.text() or t.proc.poll() is not None)
    if t.proc.poll() is None:t.send(b'q')
    t.finish(0);return
   assert pid!=t.proc.pid,(phase,pid,t.proc.pid)
   assert Path(f'/proc/{t.proc.pid}/task/{t.proc.pid}/children').read_text().split()==[str(pid)],'startup must use one read worker'
   t.until(lambda:'Loading directory' in t.text() and 'Esc / F10' in t.text())
   first=(time.monotonic()-t.started)*1000
   assert str(root)[:min(size[0]-3,len(str(root)))] in t.text() if not implicit else '.' in t.text()
   if second:assert 'Right panel' in t.text(),t.text()
   sent=time.monotonic();resize_ack=None
   if action=='esc':t.send(b'\x1b')
   elif action=='f10':t.send(b'\x1b[21~')
   elif action in ['SIGINT','SIGTERM','SIGHUP']:t.proc.send_signal(getattr(signal,action))
   elif action=='delete':
    for p in root.iterdir():p.unlink()
    root.rmdir();release(gate)
   else:
    if action=='resize':
     t.resize((35,5));t.until(lambda:'Loading directory' in t.text() and 'Esc / F10' in t.text());t.resize((100,24));t.until(lambda:'Esc / F10' in t.text());resize_ack=(time.monotonic()-sent)*1000
    # These commands must be discarded, including near the completion boundary.
    t.send(b'\x1bOQbad-name\n\x1b[15~\x1b[19~jjjj\t')
    release(gate);t.until(lambda:'> a.txt' in t.text() and 'Enter' in t.text() and 'Loading directory' not in t.text())
    assert '.hidden' not in t.text() and not (root/'bad-name').exists()
    assert '> a.txt' in t.text(),t.text()
    if mode==2:t.until(lambda:'Left*' in t.text());t.send(b'\t');t.until(lambda:'Right*' in t.text())
    if mode==1 and size[0]>50:t.until(lambda:'startup payload' in t.text())
    t.until(lambda:not Path(f'/proc/{pid}').exists(),.6)
    t.send(b'q')
   t.finish(1 if action=='delete' else 0)
   elapsed=(time.monotonic()-sent)*1000
   assert elapsed<600,(action,elapsed)
   t.until(lambda:not Path(f'/proc/{pid}').exists(),.6)
   worker_gone=(time.monotonic()-sent)*1000
   assert (not root.exists()) if action=='delete' else (root/'a.txt').read_text()=='startup payload\n'
   print(f'PASS startup {phase} mode={mode} {size} {action} implicit={implicit} second={second}: first-frame={first:.1f}ms control-to-exit={elapsed:.1f}ms resize-frame={resize_ack}ms worker-gone={worker_gone:.1f}ms worker={pid}',flush=True)
  finally:release(gate);t.close()

def normal(size,mode,kind):
 with tempfile.TemporaryDirectory(prefix='tfile-startup-normal-') as tmp:
  base=Path(tmp);root,env=fixture(base,mode)
  arg=str(root) if kind=='explicit' else '.' if kind=='relative' else str(root/'missing') if kind=='missing' else str(root/'a.txt') if kind=='file' else str(root/'denied')
  if kind=='denied':Path(arg).mkdir();Path(arg).chmod(0)
  t=Session(root,[] if kind=='implicit' else [arg],env,size)
  try:
   if kind in ['missing','file','denied']:
    t.finish(1);assert b'No such file' in t.raw if kind=='missing' else b'Not a directory' in t.raw if kind=='file' else b'Permission denied' in t.raw
   else:
    if size[0]<50:t.until(lambda:bool(t.raw));t.resize((100,24))
    t.until(lambda:'> a.txt' in t.text() and 'Enter' in t.text());elapsed=(time.monotonic()-t.started)*1000
    assert b'Loading directory' not in t.raw,'unnecessary loading frame on fast local fixture'
    if mode==2:t.until(lambda:'Left*' in t.text())
    if mode==1 and size[0]>50:t.until(lambda:'startup payload' in t.text())
    t.send(b'q');t.finish(0)
    print(f'NORMAL {"before" if BASELINE else "after"} startup {size} mode={mode} {kind}: first-list={elapsed:.1f}ms',flush=True)
   if kind in ['missing','file','denied']:print(f'PASS startup error {kind} {size}',flush=True)
  finally:t.close();(root/'denied').chmod(0o700) if kind=='denied' else None

if __name__=='__main__':
 for size in [(50,9),(100,24)]:
  for phase in ['startup-resolve','browse','entries']:
   for action in ['esc','f10','resize']:
    gated(size,1,phase,action)
  gated(size,0,'startup-resolve','esc',implicit=True)
  gated(size,2,'startup-resolve','f10',second=True)
  if not BASELINE:
   gated(size,2,'startup-resolve','resize',second=True)
   gated(size,2,'startup-resolve','delete',second=True)
   for mode in [0,1,2]:normal(size,mode,'explicit')
   for kind in ['implicit','relative','missing','file','denied']:normal(size,0,kind)
 if not BASELINE:
  for sig in ['SIGINT','SIGTERM','SIGHUP']:gated((100,24),2,'entries',sig)
  gated((100,24),0,'browse','delete')
  normal((35,5),2,'explicit')
  gated((35,5),0,'browse','resize')
  for _ in range(10):gated((50,9),0,'startup-resolve','esc',implicit=True)
  with tempfile.TemporaryDirectory(prefix='tfile-startup-cli-') as tmp:
   root,env=fixture(Path(tmp),0)
   for args,expected in [([],b'interactive'),(['a','b'],b'Usage:')]:
    result=subprocess.run([BINARY]+args,env=env,input=b'',stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    assert result.returncode==1 and expected.lower() in result.stderr.lower(),result
   print('PASS noninteractive refusal and argument errors',flush=True)
