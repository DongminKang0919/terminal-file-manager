"""Actual exec of a fixture Vim; native policy coverage, not a real Vim terminal."""
import json,os,subprocess,sys,tempfile
from pathlib import Path
VIM_STUB='#!'+sys.executable+'''\nimport json,os,signal,sys
from pathlib import Path
with open(os.environ['TFILE_VIM_LOG'],'a') as f:f.write(json.dumps(sys.argv[1:])+'\\n')
if os.environ.get('TFILE_VIM_MODE')=='fail':sys.exit(9)
if os.environ.get('TFILE_VIM_MODE')=='signal':os.kill(os.getpid(),signal.SIGTERM)
'''
if __name__=='__main__':
 with tempfile.TemporaryDirectory(prefix='tfile-vim-native-') as d:
  root=Path(d);tools=root/'tools';tools.mkdir();empty=root/'empty';empty.mkdir()
  (tools/'vim').write_text(VIM_STUB);(tools/'vim').chmod(0o700)
  file=root/'- 한글 $(touch marker)';file.write_text('original\n');log=root/'log'
  env={**os.environ,'PATH':str(tools),'TFILE_VIM_LOG':str(log)}
  subprocess.run([os.environ.get('TFILE_VIM_TEST','./tests/vim_test'),str(file),str(root),str(empty)],env=env,check=True)
  records=[json.loads(x) for x in log.read_text().splitlines()]
  assert records[0]==['--',str(file)] and records[1]==['-R','--',str(root/'checking')]
  assert len(records)==4 and file.read_text()=='original\n' and not (root/'marker').exists()
 print('PASS: exact Vim argv (including -R/--), literal hostile filename, no shell or fallback')
