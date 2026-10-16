"""A queued Browse supersedes Run even when destination validation just finished."""
import os
from pathlib import Path
import tempfile
import display_pty
from ui_io_latency_pty import release
from preview_latency_pty import until,text

display_pty.BINARY=os.path.abspath(os.environ.get('TFILE_IO_BINARY','tests/tfile_ui_io'))
old='before' in display_pty.BINARY
label='[ Copy now ]' if old else '[ Copy 1 item ]'
for width,height in [(50,9),(100,24)]:
 with tempfile.TemporaryDirectory(prefix='tfile-ux-browse-race-') as temp:
  base=Path(temp);root=base/'source';root.mkdir();dest=base/'old';dest.mkdir();new=base/'new';new.mkdir()
  (root/'a.txt').write_text('source payload\n')
  gate=base/'gate';marker=base/'gate.entered';os.mkfifo(str(gate)+'.release')
  config=base/'config/tfile';config.mkdir(parents=True);(config/'settings.conf').write_text('version=1\nimage=0\n')
  os.environ.update(HOME=str(base/'home'),XDG_CONFIG_HOME=str(base/'config'),XDG_DATA_HOME=str(base/'data'),XDG_CACHE_HOME=str(base/'cache'),TFILE_IO_PHASE='validation-ready',TFILE_IO_GATE=str(gate),TFILE_IO_MATCH=str(dest))
  t=display_pty.Terminal(str(root),width,height)
  try:
   t.send('\x1b[15~'+'\x1bOH'+'\x1b[3~'*300+str(dest))
   gate.touch();os.write(t.master,b'\n\n\n')
   until(t,lambda:marker.exists() and bool(marker.read_text().strip()))
   assert int(marker.read_text().split()[0])==t.proc.pid
   gate.unlink();os.write(t.master,('\x1b[Z'*3+'\n').encode());release(gate)
   until(t,lambda:'Choose destination' in text(t));t.send('p'+'\x1bOH'+'\x1b[3~'*300+str(new)+'\n')
   until(t,lambda:'No subdirectories' in text(t));t.send(' ')
   until(t,lambda:'Choose destination' not in text(t) and (label in text(t) or '[Success]' in text(t)))
   # Send a genuine next focus event; no delayed Run may survive Browse.
   t.send('\t');until(t,lambda:label in text(t) or '[Success]' in text(t))
   assert not (dest/'a.txt').exists(),f'obsolete Run copied to old destination: {(dest/"a.txt").read_text()!r}'
   assert not list(new.iterdir()) and label in text(t),text(t)
   assert '/new' in text(t),text(t)
   # New execution requires an explicit action and must use the edited path.
   for y in range(height):
    row=t.screen.row(y)
    if label in row:t.click(row.index(label)+2,y);break
   until(t,lambda:(new/'a.txt').exists())
   assert (new/'a.txt').read_text()=='source payload\n' and not list(dest.iterdir()) and (root/'a.txt').exists()
   print(f'PASS queued Browse {width}x{height}: prior Run discarded, explicit action uses new path, source preserved',flush=True)
  finally:
   release(gate)
   for key in ['TFILE_IO_PHASE','TFILE_IO_GATE','TFILE_IO_MATCH']:os.environ.pop(key,None)
   for _ in range(3):t.send('\x1b')
   t.close()
