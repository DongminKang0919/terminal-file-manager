"""Isolated ASan/UBSan builds for changed preview/process/UI code."""
import argparse
import os
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-directory',default='/tmp/tfile-media-sanitizers')
parser.add_argument('--disable-leaks',action='store_true',help='Explicitly omit LSan, e.g. under ptrace')
args=parser.parse_args()
root=Path(__file__).resolve().parents[1];os.chdir(root)
out=Path(args.output_directory).resolve();out.mkdir(parents=True,exist_ok=True)
sources=['src/model.c']+sorted(str(p) for p in Path('src/core').glob('*.c'))+['src/platform/posix.c','src/platform/posix_media.c']+sorted(str(p) for p in Path('src/ui').glob('*.c') if p.name!='main.c')
flags=[os.environ.get('CC','cc'),'-D_XOPEN_SOURCE=700','-std=c11','-g','-O1','-Wall','-Wextra','-Wpedantic',
       '-fsanitize=address,undefined','-fno-omit-frame-pointer']
wraps='platform_reader_open,platform_reader_line,platform_reader_close,platform_reader_changed,readdir,platform_directory_empty,platform_directory_open,platform_info,app_refresh'
for target,test,extra in [('media_test','tests/media_test.c',['-Wl,--wrap=platform_monotonic_ms']),
                          ('preview_test','tests/preview_test.c',['-Wl,'+','.join('--wrap='+x for x in wraps.split(','))]),
                          ('tfile','src/ui/main.c',[])]:
    subprocess.run(flags+['-o',str(out/target),test]+sources+['-lncursesw']+extra,check=True)
env={**os.environ,'ASAN_OPTIONS':f'detect_leaks={int(not args.disable_leaks)}:halt_on_error=1',
     'UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1','TFILE_MEDIA_TEST':str(out/'media_test'),'TFILE_BINARY':str(out/'tfile')}
failed=[]
for name,command in [('media',[os.environ.get('PYTHON','python3'),'tests/media_tools.py']),
                     ('preview',[str(out/'preview_test')]),
                     ('media_pty',[os.environ.get('PYTHON','python3'),'tests/media_pty.py'])]:
    with (out/f'{name}.log').open('w') as log:
        result=subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT)
    print(('PASS' if result.returncode==0 else 'FAIL')+f': {name}, {out/name}.log',flush=True)
    if result.returncode:failed.append(name)
print('Leak detection '+('explicitly disabled' if args.disable_leaks else 'requested')+'.',flush=True)
raise SystemExit(bool(failed))
