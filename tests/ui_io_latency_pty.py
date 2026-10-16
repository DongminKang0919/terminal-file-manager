"""Gate-controlled UI filesystem latency. --baseline records the old UI stalls."""
import fcntl
import os
from pathlib import Path
import select
import signal
import struct
import sys
import tempfile
import termios
import time
import display_pty
from preview_latency_pty import pump,text,until

BASELINE='--baseline' in sys.argv
display_pty.BINARY=os.path.abspath(os.environ.get('TFILE_IO_BINARY','/tmp/tfile-ui-io-before' if BASELINE else 'tests/tfile_ui_io'))
F5='\x1b[15~';F10=b'\x1b[21~';HOME='\x1bOH';DOWN='\x1bOB'
phases=['resolve','browse','create-before','create-after','media-source','tools']
if os.environ.get('TFILE_IO_CASE'):phases=[os.environ['TFILE_IO_CASE']]

def release(gate):
 gate.unlink(missing_ok=True)
 try:fd=os.open(str(gate)+'.release',os.O_WRONLY|os.O_NONBLOCK)
 except OSError:return
 os.write(fd,b'x');os.close(fd)

def gone(pid):return not Path(f'/proc/{pid}').exists()

def run_latency():
 for width,height in [(50,9),(100,24)]:
  for phase in phases:
   for control in ['esc','exit','resize']:
    with tempfile.TemporaryDirectory(prefix='tfile-ui-io-') as temp:
     base=Path(temp);root=base/'root';root.mkdir();dest=base/'dest';dest.mkdir();tools=base/'io-tools';tools.mkdir()
     (root/'a.txt').write_text('unchanged source\n');(root/'b.pdf').write_text('%PDF-1.4\nfixture\n');(root/'c.txt').write_text('latest selection\n')
     tool=tools/'pdftotext';tool.write_text('#!/bin/sh\nprintf "prepared PDF text\\n"\n');tool.chmod(0o755)
     home=base/'home';home.mkdir();config=base/'config/tfile';config.mkdir(parents=True);(config/'settings.conf').write_text('version=1\nimage=0\n')
     gate=base/'gate';marker=base/'gate.entered';os.mkfifo(str(gate)+'.release')
     match=str(dest) if phase in ['resolve','browse'] else 'created' if phase.startswith('create') else str(root/'b.pdf') if phase=='media-source' else str(tools)
     os.environ.update(HOME=str(home),XDG_CONFIG_HOME=str(base/'config'),XDG_DATA_HOME=str(base/'data'),XDG_CACHE_HOME=str(base/'cache'),PATH=str(tools)+':'+os.environ['PATH'],TFILE_IO_PHASE=phase,TFILE_IO_GATE=str(gate),TFILE_IO_MATCH=match)
     t=display_pty.Terminal(str(root),width,height)
     try:
      until(t,lambda:'unchanged source' in text(t) or width==50)
      gate.touch()
      if phase in ['resolve','browse']:
       t.send(F5)
       t.send(HOME+'\x1b[3~'*200+str(dest))
       os.write(t.master,('\n\n\n' if phase=='resolve' else '\t\t\n').encode())
      elif phase.startswith('create'):os.write(t.master,b'\x1bOQcreated\n')
      elif phase=='media-source':os.write(t.master,b'j')
      else:os.write(t.master,('\x1b[18~'+DOWN*7+'\n').encode())
      until(t,lambda:marker.exists() and bool(marker.read_text().strip()))
      pid=int(marker.read_text().split()[0]);gate.unlink()
      start=time.monotonic()
      if control=='exit':os.write(t.master,F10)
      elif control=='esc':os.write(t.master,b'j\x1b' if phase=='media-source' else b'\x1b')
      else:
       t.screen=display_pty.Screen(height+1,width+1)
       fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',height+1,width+1,0,0));t.proc.send_signal(signal.SIGWINCH)
      if BASELINE:
       end=start+.2
       while time.monotonic()<end:pump(t)
       assert pid==t.proc.pid and t.proc.poll() is None,(phase,pid,t.proc.pid)
       observed=(time.monotonic()-start)*1000
       release(gate)
       print(f'BEFORE {phase} {width}x{height} {control}: UI pid={pid}, input blocked >= {observed:.1f}ms until explicit release',flush=True)
      else:
       assert pid!=t.proc.pid,(phase,pid)
       if phase.startswith('create'):
        until(t,lambda:'Cancellation requested' in text(t),.6)
        ack=(time.monotonic()-start)*1000
        assert t.proc.poll() is None and not gone(pid)
        assert (root/'created').exists()==(phase=='create-after')
        release(gate)
        until(t,lambda:gone(pid),.6)
        reaped_ms=(time.monotonic()-start)*1000
        if control=='exit':until(t,lambda:t.proc.poll() is not None,.6)
        assert (root/'created').exists()==(phase=='create-after')
        if phase=='create-after' and control!='exit':
         until(t,lambda:'File created' in text(t) or '[Success]' in text(t),.6)
         assert 'Cancellation requested' not in text(t)
       else:
        if phase=='media-source' and control=='resize':
         # PDF text is geometry-independent: observe resize, then select a new
         # file to cancel the still-held preparation without releasing its gate.
         until(t,lambda:bool(t.screen.row(height).strip()),.6)
         resize_ack=(time.monotonic()-start)*1000
         os.write(t.master,b'j')
        until(t,lambda:gone(pid),.6)
        reaped_ms=(time.monotonic()-start)*1000
        if control=='exit':until(t,lambda:t.proc.poll() is not None,.6)
        elif phase=='resolve' and control=='esc':until(t,lambda:'check cancelled' in text(t),.6)
        elif control=='resize':until(t,lambda:bool(t.screen.row(height).strip()),.6)
        elif phase=='browse':until(t,lambda:'Choose destination' not in text(t) and '[ Browse ]' in text(t),.6)
        elif phase=='tools':until(t,lambda:'Options' in text(t) and 'Image tool:' not in text(t),.6)
        elif phase=='media-source':until(t,lambda:any('> c.txt' in t.screen.row(y) for y in range(t.screen.height)) and 'PDF page 1' not in text(t),.6)
        ack=resize_ack if phase=='media-source' and control=='resize' else (time.monotonic()-start)*1000
       if control=='exit':assert t.proc.returncode==0
       elapsed=(time.monotonic()-start)*1000
       print(f'PASS {phase} {width}x{height} {control}: worker={pid}, frame/exit={ack:.1f}ms, worker reaped={reaped_ms:.1f}ms, observed completion={elapsed:.1f}ms',flush=True)
      assert (root/'a.txt').read_text()=='unchanged source\n'
      assert not list(dest.iterdir())
     finally:
      release(gate)
      if t.proc.poll() is None:
       for _ in range(4):t.send('\x1b')
       t.close()
      else:os.close(t.master)
      for key in ['TFILE_IO_PHASE','TFILE_IO_GATE','TFILE_IO_MATCH']:os.environ.pop(key,None)

if __name__=='__main__':run_latency()
