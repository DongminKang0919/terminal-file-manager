"""Reproducible PTY cell records, not photographs or terminal color validation."""
from contextlib import nullcontext
import json
import os
from pathlib import Path
import sys
import tempfile
import display_pty
from preview_latency_pty import until,text

display_pty.BINARY=os.path.abspath(os.environ.get("TFILE_BINARY","./tfile"))
output=Path(sys.argv[1]);output.mkdir(parents=True,exist_ok=True)
records=[]
fixture=os.environ.get('TFILE_UX_FIXTURE')
if fixture:assert fixture.startswith('/tmp/tfile-ux-fixture-')
with (nullcontext(fixture) if fixture else tempfile.TemporaryDirectory(prefix='tfile-ux-capture-')) as temp:
 base=Path(temp);root=base/'source';root.mkdir(exist_ok=True);dest=base/'한글-destination';dest.mkdir(exist_ok=True)
 for name in ['a.txt','b-한글.txt','c.txt']:
  path=root/name
  if not path.exists():path.write_text('Preview body\n'*60)
  assert path.read_text()=='Preview body\n'*60
  os.utime(path,(1700000000,1700000000))
 config=base/'config/tfile';config.mkdir(parents=True,exist_ok=True)
 os.environ.update(HOME=str(base/'home'),XDG_CONFIG_HOME=str(base/'config'),XDG_DATA_HOME=str(base/'data'),XDG_CACHE_HOME=str(base/'cache'))
 for width,height in [(50,9),(100,24)]:
  for mode in [0,1,2]:
   (config/'settings.conf').write_text(f'version=1\nmode={mode}\nimage=0\n')
   t=display_pty.Terminal(str(root),width,height)
   def capture(name):
    # A completed frame footer, rather than a fixed delay, identifies the state.
    t.read()
    records.append({'size':[width,height],'mode':mode,'state':name,'rows':[t.screen.row(y) for y in range(height)]})
   try:
    until(t,lambda:'a.txt' in text(t));capture('cursor')
    t.send(' j');capture('one-mark-cursor-elsewhere')
    t.send(' j');capture('two-marks-cursor-elsewhere')
    t.send('\x1b[15~');capture('copy-two-destination')
    t.send('\x1bOH'+'\x1b[3~'*300+str(base/'missing')+'\n');until(t,lambda:'Destination:' in text(t));capture('copy-two-destination-error')
    t.send('\x1bOH'+'\x1b[3~'*300+str(root)+'\n');until(t,lambda:'confirmation' in text(t));capture('copy-two-review-default-cancel')
    t.send('\x1b');capture('copy-two-checked');t.send('\x1b')
    t.send('\x1b[17~');capture('move-two-destination');t.send('\x1b')
    t.send('u\x1bOH\x1b[15~');capture('copy-one')
    t.send('\x1bOH'+'\x1b[3~'*300+str(base/'missing')+'\n\n\n');until(t,lambda:'Destination:' in text(t));capture('copy-one-destination-error')
    t.send('\x1b');t.send('\x1b[17~');capture('move-one');t.send('\x1b')
    if mode==1:
     t.send('\t');capture('preview-focus');t.send('\t')
    assert not list(dest.iterdir()) and len(list(root.iterdir()))==3
   finally:t.close()
 gated=os.environ.get('TFILE_UX_GATED_BINARY')
 if gated:
  mode=1
  display_pty.BINARY=os.path.abspath(gated)
  for width,height in [(50,9),(100,24)]:
   for multiple in [False,True]:
    (config/'settings.conf').write_text('version=1\nmode=1\nimage=0\n')
    gate=base/'gate';marker=base/'gate.entered'
    marker.unlink(missing_ok=True)
    release=Path(str(gate)+'.release');release.unlink(missing_ok=True);os.mkfifo(release);gate.touch()
    os.environ.update(TFILE_IO_PHASE='resolve',TFILE_IO_GATE=str(gate),TFILE_IO_MATCH=str(dest))
    t=display_pty.Terminal(str(root),width,height)
    try:
     if multiple:t.send(' j ')
     t.send('\x1b[15~'+'\x1bOH'+'\x1b[3~'*300+str(dest)+('\n' if multiple else '\n\n\n'))
     until(t,lambda:marker.exists() and bool(marker.read_text().strip()) and 'Checking destination' in text(t))
     capture('copy-'+('two' if multiple else 'one')+'-checking')
     t.send('\x1b');until(t,lambda:'check cancelled' in text(t));capture('copy-'+('two' if multiple else 'one')+'-check-cancelled')
     assert not list(dest.iterdir()) and len(list(root.iterdir()))==3
    finally:
     gate.unlink(missing_ok=True)
     try:
      fd=os.open(release,os.O_WRONLY|os.O_NONBLOCK);os.write(fd,b'x');os.close(fd)
     except OSError:pass
     t.close()
     for key in ['TFILE_IO_PHASE','TFILE_IO_GATE','TFILE_IO_MATCH']:os.environ.pop(key,None)
(output/'screens.json').write_text(json.dumps(records,ensure_ascii=False,indent=2)+'\n')
lines=['# PTY cell records','', 'Paths and terminal cells are kept verbatim. No colors are represented.','']
for r in records:lines.extend([f'## {r["size"][0]}x{r["size"][1]} mode={r["mode"]} {r["state"]}','', '```text',*r['rows'],'```',''])
(output/'screens.md').write_text('\n'.join(line.rstrip() for line in lines)+'\n')
print(f'PASS {len(records)} PTY records; no file operations executed; {output}',flush=True)
