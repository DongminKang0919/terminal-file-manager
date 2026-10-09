"""Parent-only termination: controlled editor, exact child, termios and reap."""
import os,signal,sys,tempfile,time,termios
from pathlib import Path
from display_pty import Terminal

with tempfile.TemporaryDirectory(prefix='tfile-editor-shutdown-') as d:
 root=Path(d);files=root/'files';files.mkdir();(files/'text').write_text('unchanged\n')
 tools=root/'tools';tools.mkdir();record=root/'record'
 stub='#!'+sys.executable+'''\nimport os,signal,time,termios
from pathlib import Path
p=Path(os.environ['TFILE_EDITOR_RECORD'])
def stop(n,frame):
 p.write_text(str(n))
 time.sleep(.5)
 raise SystemExit(0)
signal.signal(signal.SIGTERM,stop);signal.signal(signal.SIGHUP,stop)
a=termios.tcgetattr(0);a[3]&=~(termios.ICANON|termios.ECHO);termios.tcsetattr(0,termios.TCSANOW,a)
p.write_text('ready')
while True:time.sleep(.1)
'''
 (tools/'vim').write_text(stub);(tools/'vim').chmod(0o700)
 os.environ.update(PATH=str(tools),TFILE_EDITOR_RECORD=str(record),XDG_CONFIG_HOME=str(root/'config'))
 for sig in (signal.SIGTERM,signal.SIGHUP):
  record.unlink(missing_ok=True);t=Terminal(str(files));child=None
  try:
   t.send('e');end=time.monotonic()+5
   while not record.exists():
    assert time.monotonic()<end;t.read()
   child=int(Path(f'/proc/{t.proc.pid}/task/{t.proc.pid}/children').read_text().split()[0])
   os.kill(t.proc.pid,sig)
   end=time.monotonic()+5
   while record.read_text()!=str(sig):
    assert time.monotonic()<end;t.read()
   assert t.proc.poll() is None,'parent did not give editor time to exit'
   assert t.proc.wait(timeout=5)==0;t.read()
   assert record.read_text()==str(sig),'editor did not receive parent shutdown request'
   assert not Path(f'/proc/{child}').exists(),'editor remains alive or zombie'
   flags=termios.tcgetattr(t.master)[3]
   assert flags&termios.ICANON and flags&termios.ECHO,'shell input modes not restored'
   assert (files/'text').read_text()=='unchanged\n'
  finally:
   if child and Path(f'/proc/{child}').exists():
    os.kill(child,signal.SIGKILL)
   if t.proc.poll() is None:t.proc.kill();t.proc.wait()
   os.close(t.master)
 print('PASS: parent TERM/HUP forwarded only to editor, grace/reap and shell termios restored')
