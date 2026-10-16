"""Ordinary local fixture runs, separate from synthetic held-call measurements."""
import os
from pathlib import Path
import sys
import tempfile
import time
import display_pty
from preview_latency_pty import text,until

before='--baseline' in sys.argv
display_pty.BINARY=os.path.abspath('/tmp/tfile-ui-io-before' if before else os.environ.get('TFILE_IO_BINARY','tests/tfile_ui_io'))
for width,height in [(50,9),(100,24)]:
 for phase in ['transfer','browse','create','media','tools']:
  with tempfile.TemporaryDirectory(prefix='tfile-ui-normal-') as temp:
   base=Path(temp);root=base/'root';root.mkdir();dest=base/'dest';dest.mkdir();tools=base/'tools';tools.mkdir()
   (root/'a.txt').write_text('source payload\n');(root/'b.pdf').write_text('%PDF-1.4\n')
   tool=tools/'pdftotext';tool.write_text('#!/bin/sh\nprintf "ordinary PDF text\\n"\n');tool.chmod(0o700)
   config=base/'config/tfile';config.mkdir(parents=True);(config/'settings.conf').write_text('version=1\nimage=0\n')
   os.environ.update(HOME=str(base/'home'),XDG_CONFIG_HOME=str(base/'config'),XDG_DATA_HOME=str(base/'data'),XDG_CACHE_HOME=str(base/'cache'),PATH=str(tools))
   for key in ['TFILE_IO_PHASE','TFILE_IO_GATE','TFILE_IO_MATCH']:os.environ.pop(key,None)
   t=display_pty.Terminal(str(root),width,height)
   try:
    if phase in ['transfer','browse']:t.send('\x1b[15~'+'\x1bOH'+'\x1b[3~'*200+str(dest))
    start=time.monotonic()
    if phase=='transfer':
     os.write(t.master,b'\n\n\n');until(t,lambda:(dest/'a.txt').exists())
     assert (dest/'a.txt').read_text()=='source payload\n'
    elif phase=='browse':os.write(t.master,b'\t\t\n');until(t,lambda:'No subdirectories' in text(t))
    elif phase=='create':os.write(t.master,b'\x1bOQcreated\n');until(t,lambda:(root/'created').exists())
    elif phase=='media':os.write(t.master,b'j');until(t,lambda:'ordinary PDF text' in text(t) if width>50 else 'PDF page 1' in text(t))
    else:os.write(t.master,('\x1b[18~'+'\x1bOB'*7+'\n').encode());until(t,lambda:'Image tool:' in text(t))
    print(f'NORMAL {"before" if before else "after"} {phase} {width}x{height}: observable result {(time.monotonic()-start)*1000:.1f}ms (no gate; includes PTY delivery)',flush=True)
    assert (root/'a.txt').read_text()=='source payload\n'
   finally:
    for _ in range(4):t.send('\x1b')
    t.close()
