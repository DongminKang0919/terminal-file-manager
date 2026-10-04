"""ASan/UBSan for terminal parsing, filtered input and the real diagnostic UI."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-directory',default='/tmp/tfile-terminal-sanitizers')
parser.add_argument('--disable-leaks',action='store_true')
args=parser.parse_args()
root=Path(__file__).resolve().parents[1];os.chdir(root)
out=Path(args.output_directory).resolve();out.mkdir(parents=True,exist_ok=True)
core=['src/model.c']+sorted(str(p) for p in Path('src/core').glob('*.c'))+['src/platform/posix.c','src/platform/posix_media.c']
ui=sorted(str(p) for p in Path('src/ui').glob('*.c') if p.name!='main.c')
flags=[os.environ.get('CC','cc'),'-D_XOPEN_SOURCE=700','-std=c11','-O1','-g','-Wall','-Wextra','-Wpedantic',
       '-fsanitize=address,undefined','-fno-omit-frame-pointer']
for name,test,sources,extra in [
    ('terminal_test','tests/terminal_test.c',core,[]),
    ('terminal_input_test','tests/terminal_input_test.c',core+ui,['-lncursesw','-Wl,--wrap=core_monotonic_ms']),
    ('tfile','src/ui/main.c',core+ui,['-lncursesw'])]:
    subprocess.run(flags+['-o',str(out/name),test]+sources+extra,check=True)
with tempfile.TemporaryDirectory(prefix='tfile-terminal-san-home-') as home:
    env={**os.environ,'HOME':home,'XDG_CONFIG_HOME':home+'/config',
         'ASAN_OPTIONS':f'detect_leaks={int(not args.disable_leaks)}:halt_on_error=1',
         'UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1','TFILE_BINARY':str(out/'tfile')}
    for name,command in [('terminal',[str(out/'terminal_test')]),('input',[str(out/'terminal_input_test')]),
                         ('pty',['python3','tests/terminal_pty.py'])]:
        with (out/(name+'.log')).open('w') as log:
            result=subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT)
        print(('PASS' if result.returncode==0 else 'FAIL')+f': {name}, {out/name}.log',flush=True)
        if result.returncode:raise SystemExit(result.returncode)
print('LSan '+('disabled; incomplete' if args.disable_leaks else 'requested')+'.')
