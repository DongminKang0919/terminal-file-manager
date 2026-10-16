"""Request replacement, Browse recovery and execution-time revalidation."""
import os
from pathlib import Path
import tempfile
import time
import display_pty
from ui_io_latency_pty import release,gone,F5,HOME
from preview_latency_pty import text,until

class QueryScreen(display_pty.Screen):
 def csi(self,params,command):
  if command=='c':return # Device capability query does not paint cells.
  super().csi(params,command)
display_pty.Screen=QueryScreen

display_pty.BINARY=os.path.abspath(os.environ.get('TFILE_IO_BINARY','tests/tfile_ui_io'))
BACKTAB='\x1b[Z'
def replace(value):return HOME+'\x1b[3~'*200+str(value)
def close(t):
 if t.proc.poll() is None:
  for _ in range(3):t.send('\x1b')
  t.close()
 else:os.close(t.master)

for width,height in [(50,9),(100,24)]:
 for case in ['edit','queued-edit','collision','replaced','denied','deleted','media-stale','tools-query']:
  if os.environ.get('TFILE_IO_LIFECYCLE_CASE') not in (None,case):continue
  with tempfile.TemporaryDirectory(prefix='tfile-ui-lifecycle-') as temp:
   base=Path(temp);root=base/'root';root.mkdir();dest=base/'dest';dest.mkdir();new=base/'new';new.mkdir()
   (root/'a.txt').write_text('source payload\n')
   if case=='media-stale':
    (root/'b.pdf').write_text('%PDF-1.4\n');(root/'c.txt').write_text('latest content\n')
    tools=base/'tools';tools.mkdir();tool=tools/'pdftotext';tool.write_text('#!/bin/sh\nprintf "obsolete PDF text\\n"\n');tool.chmod(0o700)
    os.environ['PATH']=str(tools)+':'+os.environ['PATH']
   if case=='tools-query':
    tools=base/'tools';tools.mkdir();os.environ['PATH']=str(tools)
   config=base/'config/tfile';config.mkdir(parents=True);(config/'settings.conf').write_text('version=1\nimage=0\n')
   os.environ.update(HOME=str(base/'home'),XDG_CONFIG_HOME=str(base/'config'),XDG_DATA_HOME=str(base/'data'),XDG_CACHE_HOME=str(base/'cache'))
   gate=base/'gate';marker=base/'gate.entered';os.mkfifo(str(gate)+'.release')
   phase='tools' if case=='tools-query' else 'validation-ready' if case=='queued-edit' else 'media-result' if case=='media-stale' else 'resolve' if case=='edit' else 'resolve-after'
   os.environ.update(TFILE_IO_GATE=str(gate),TFILE_IO_PHASE=phase,TFILE_IO_MATCH=str(tools) if case=='tools-query' else str(root/'b.pdf') if case=='media-stale' else str(dest))
   t=display_pty.Terminal(str(root),width,height)
   try:
    if case=='tools-query':
     gate.touch();os.write(t.master,('\x1b[18~'+'\x1bOB'*7+'\n').encode())
     until(t,lambda:marker.exists() and bool(marker.read_text().strip()));worker=int(marker.read_text().split()[0]);gate.unlink()
     t.send('\n');until(t,lambda:'Auto check inconclusive' in text(t))
     assert not gone(worker) and 'Checking media tools' in text(t),text(t)
     tool=tools/'magick';tool.write_text('#!/bin/sh\nexit 0\n');tool.chmod(0o700);release(gate)
     until(t,lambda:'magick (found' in text(t))
     t.send('\x1b');tool.unlink();t.send('\n');until(t,lambda:'missing; install' in text(t))
     t.send('\x1b');tool.write_text('#!/bin/sh\nexit 0\n');tool.chmod(0o700);t.send('\n');until(t,lambda:'magick (found' in text(t))
     print(f'PASS tools-query {width}x{height}: query while discovery held, missing/install/remove/recheck without permanent cache',flush=True)
     continue
    if case=='media-stale':
     gate.touch();os.write(t.master,b'j')
     until(t,lambda:marker.exists() and bool(marker.read_text().strip()))
     worker=int(marker.read_text().split()[0]);assert worker!=t.proc.pid;gate.unlink()
     assert all(str(root/'b.pdf')!=os.readlink(p) for p in Path(f'/proc/{t.proc.pid}/fd').iterdir())
     os.write(t.master,b'j');until(t,lambda:gone(worker),.6)
     until(t,lambda:any('> c.txt' in t.screen.row(y) for y in range(t.screen.height)))
     if width>50:until(t,lambda:'latest content' in text(t))
     assert 'obsolete PDF text' not in text(t) and 'PDF page 1' not in text(t)
     print(f'PASS media-stale {width}x{height}: completed old capture discarded before reply; no source FD in UI',flush=True)
     continue
    t.send(F5+replace(dest))
    if case in ['denied','deleted']:
     if case=='denied':dest.chmod(0)
     else:dest.rmdir()
     t.send('\t\t\n');until(t,lambda:'Cannot read directory' in text(t))
     assert 'You can use this directory' not in text(t)
     t.send('p');t.send(replace(new)+'\n');until(t,lambda:'No subdirectories' in text(t))
     t.send(' ');until(t,lambda:'/new' in text(t) and 'Choose destination' not in text(t))
     start=time.monotonic();os.write(t.master,('\t'*3+'\n').encode());until(t,lambda:(new/'a.txt').exists())
     print(f'NORMAL recovered transfer {width}x{height}: file observed {(time.monotonic()-start)*1000:.1f}ms (no gate)',flush=True)
     assert (new/'a.txt').read_text()=='source payload\n'
    else:
     gate.touch();os.write(t.master,b'\n\n\n')
     until(t,lambda:marker.exists() and bool(marker.read_text().strip()))
     worker=int(marker.read_text().split()[0]);assert (worker==t.proc.pid)==(case=='queued-edit');gate.unlink()
     if case in ['edit','queued-edit']:
      # Pending Run is invalidated by a change to either input. Repeated Enter
      # cannot start a second mutation while validation is held.
      os.write(t.master,('\n'*8+BACKTAB*5+replace(new) if case=='edit' else BACKTAB*5+replace(new)).encode())
      if case=='queued-edit':
       release(gate)
       try:until(t,lambda:'/new' in text(t),.6)
       except AssertionError:
        stale=dest/'a.txt'
        print(f'queued-edit failure: old destination exists={stale.exists()}, payload={stale.read_text() if stale.exists() else None!r}',flush=True)
        raise
      else:until(t,lambda:gone(worker),.6)
      assert not list(dest.iterdir()) and not list(new.iterdir())
      t.send('\n\n\n');until(t,lambda:(new/'a.txt').exists())
      until(t,lambda:'[ Copy now ]' not in text(t))
      assert (new/'a.txt').read_text()=='source payload\n' and not list(dest.iterdir())
     else:
      if case=='collision':(dest/'a.txt').write_text('keep collision\n')
      else:dest.rmdir();dest.write_text('replacement is not directory\n')
      release(gate)
      until(t,lambda:'Name already exists' in text(t) if case=='collision' else 'Destination:' in text(t) or 'Not a directory' in text(t))
      if case=='collision':assert (dest/'a.txt').read_text()=='keep collision\n'
      else:assert dest.read_text()=='replacement is not directory\n'
    assert (root/'a.txt').read_text()=='source payload\n'
    print(f'PASS {case} {width}x{height}: inputs/focus recover, no stale or duplicate execution, source preserved',flush=True)
   finally:
    release(gate)
    if case=='denied':dest.chmod(0o700)
    close(t)
    for key in ['TFILE_IO_GATE','TFILE_IO_PHASE','TFILE_IO_MATCH']:os.environ.pop(key,None)
