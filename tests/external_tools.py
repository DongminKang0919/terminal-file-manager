"""Real external child processes, disposable editor/viewer/files only."""
import json,os,resource,subprocess,sys,tempfile
from pathlib import Path
STUB='#!'+sys.executable+'''\nimport json,os,resource,signal,sys,time,termios
from pathlib import Path
record={'tool':Path(sys.argv[0]).name,'args':sys.argv[1:],'pid':os.getpid(),'fds':[], 'as_limit':resource.getrlimit(resource.RLIMIT_AS)[0]}
for fd in os.listdir('/proc/self/fd'):
 if int(fd)>2:
  try:record['fds'].append(os.readlink('/proc/self/fd/'+fd))
  except FileNotFoundError:pass
with open(os.environ['TFILE_EXTERNAL_LOG'],'a') as f:f.write(json.dumps(record)+'\\n')
if '--parent-int' in sys.argv:os.kill(os.getppid(),signal.SIGINT)
if '--interactive' in sys.argv:
 assert termios.tcgetattr(0)[3]&termios.ICANON
 print('\\x1b[2J\\x1b[HEDITOR_ACTIVE',flush=True);os.read(0,128)
 Path(sys.argv[-1]).write_text('edited body')
if '--fail' in sys.argv or os.environ.get('TFILE_EXTERNAL_MODE')=='fail':sys.exit(7)
if os.environ.get('TFILE_EXTERNAL_MODE')=='slow':time.sleep(.5)
Path(os.environ['TFILE_EXTERNAL_DONE']).write_text('completed')
'''
if __name__=='__main__':
 with tempfile.TemporaryDirectory(prefix='tfile-external-') as d:
  root=Path(d);tools=root/'tools';tools.mkdir();empty=root/'empty';empty.mkdir();log=root/'log';done=root/'done'
  for name in ['editor tool','vi','xdg-open']:(tools/name).write_text(STUB);(tools/name).chmod(0o700)
  file=root/'- 한글 ; $(touch marker)\n.txt';file.write_text('original');file.chmod(0o700)
  (tools/'no-shebang').write_text(': > '+str(root/'shell-ran')+'\n');(tools/'no-shebang').chmod(0o700)
  env={**os.environ,'PATH':str(tools),'TFILE_EXTERNAL_LOG':str(log),'TFILE_EXTERNAL_DONE':str(done)}
  subprocess.run([os.environ.get('TFILE_EXTERNAL_TEST','./tests/external_test'),str(file),str(root),str(empty)],env=env,check=True)
  records=[json.loads(x) for x in log.read_text().splitlines()]
  assert [r['tool'] for r in records[:4]]==['editor tool','editor tool','vi','editor tool']
  assert records[0]['args']==['--flag','quoted spaces','$(literal)',str(file)]
  assert all(r['args'][-1]==str(file) and not r['fds'] and r['as_limit']==resource.getrlimit(resource.RLIMIT_AS)[0] for r in records)
  assert file.read_text()=='original' and not (root/'marker').exists() and not (root/'shell-ran').exists() and done.exists()
 print('PASS: actual exec argv, inherited user limits, no extra FDs, original filename bytes and no shell execution')
