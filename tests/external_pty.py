"""Editor suspension/return and quiet asynchronous viewer, in disposable PTYs."""
import fcntl,json,os,signal,struct,tempfile,termios,time
from pathlib import Path
from display_pty import Terminal
from external_tools import STUB
with tempfile.TemporaryDirectory(prefix='tfile-external-pty-') as d:
 root=Path(d);files=root/'files';files.mkdir();tools=root/'tools';tools.mkdir();log=root/'log'
 for name in ['vim','xdg-open']:(tools/name).write_text(STUB.replace("if '--interactive' in sys.argv:","if Path(sys.argv[0]).name=='vim':"));(tools/name).chmod(0o700)
 target=files/'- 한글 ; $(touch marker).txt';target.write_text('original');(files/'z marked').write_text('keep')
 os.environ.update(PATH=str(tools)+':/usr/bin',VISUAL=f"'{tools}/editor tool' --interactive",TFILE_EXTERNAL_LOG=str(log),TFILE_EXTERNAL_DONE=str(root/'done'))
 t=Terminal(str(files));
 def text():return '\n'.join(t.screen.row(y) for y in range(t.screen.height))
 def wait(test):
  end=time.monotonic()+3
  while not test():assert time.monotonic()<end,text();t.read()
 try:
  t.send('\x1bOB \x1bOH'+'\x1b[20~'+'\x1bOB'*21+'\n');wait(lambda:'EDITOR_ACTIVE' in text())
  assert json.loads(log.read_text().splitlines()[0])['args']==['--',str(target)]
  assert json.loads(log.read_text().splitlines()[0])['tool']=='vim'
  fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',30,120,0,0))
  t.screen.width=120;t.screen.height=30;t.screen.rows=[[' ']*120 for _ in range(30)];t.screen.scroll_bottom=29;t.screen.x=t.screen.y=0
  os.kill(t.proc.pid,signal.SIGWINCH);t.send('\n');wait(lambda:'edited body' in text())
  assert 'Marked: 1' in text() and target.read_text()=='edited body' and not (files/'marker').exists()
  t.send('\x1b[20~'+'\x1bOB'*22+'\n');wait(lambda:'display/save unconfirmed' in text())
  assert json.loads(log.read_text().splitlines()[-1])['tool']=='xdg-open'
  t.send('r');assert 'edited body' in text() and 'Marked: 1' in text()
 finally:t.close()
print('PASS: cooked editor terminal, resize/input/media restore, cursor vs marked file, literal argv and asynchronous xdg-open result (PTY)')
