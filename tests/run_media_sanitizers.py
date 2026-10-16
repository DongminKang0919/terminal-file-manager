"""Isolated ASan/UBSan builds for changed preview/process/UI code."""
import argparse
import os
import json
import time
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-directory',default='/tmp/tfile-media-sanitizers')
parser.add_argument('--disable-leaks',action='store_true',help='Explicitly omit LSan, e.g. under ptrace')
args=parser.parse_args()
root=Path(__file__).resolve().parents[1];os.chdir(root)
out=Path(args.output_directory).resolve();out.mkdir(parents=True,exist_ok=True)
started=time.monotonic();results={}
(out/'summary.log').write_text('INCOMPLETE: required media builds/tests not finished.\n')
(out/'summary.json').write_text(json.dumps({'complete':False,'leaks_enabled':not args.disable_leaks})+'\n')
sources=['src/model.c']+sorted(str(p) for p in Path('src/core').glob('*.c'))+sorted(str(p) for p in Path('src/platform').glob('posix*.c'))+sorted(str(p) for p in Path('src/ui').glob('*.c') if p.name!='main.c')
flags=[os.environ.get('CC','cc'),'-D_XOPEN_SOURCE=700','-std=c11','-g','-O1','-Wall','-Wextra','-Wpedantic',
       '-fsanitize=address,undefined','-fno-omit-frame-pointer']
wraps='platform_reader_open,platform_reader_line,platform_reader_close,platform_reader_changed,readdir,platform_directory_empty,platform_directory_open,platform_info,app_refresh'
for target,test,extra in [('media_test','tests/media_test.c',['-Wl,--wrap=platform_monotonic_ms']),
                          ('picker_test','tests/picker_test.c',['-Wl,--wrap=input_key,--wrap=prompt_value_status']),
                          ('preview_test','tests/preview_test.c',['-Wl,'+','.join('--wrap='+x for x in wraps.split(','))]),
                          ('tfile','src/ui/main.c',[])]:
    with (out/(target+'.build.log')).open('w') as log:
        subprocess.run(flags+['-o',str(out/target),test]+sources+['-lncursesw']+extra,stdout=log,stderr=subprocess.STDOUT,check=True)
env={**os.environ,'ASAN_OPTIONS':f'detect_leaks={int(not args.disable_leaks)}:halt_on_error=1',
     'UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1','TFILE_MEDIA_TEST':str(out/'media_test'),'TFILE_BINARY':str(out/'tfile')}
failed=[]
for name,command in [('media',[os.environ.get('PYTHON','python3'),'tests/media_tools.py']),
                     ('preview',[str(out/'preview_test')]),
                     ('picker',[str(out/'picker_test')]),
                     ('media_pty',[os.environ.get('PYTHON','python3'),'tests/media_pty.py']),
                     ('media_lifecycle_pty',[os.environ.get('PYTHON','python3'),'tests/media_lifecycle_pty.py']),
                     ('media_lifecycle_fault',[os.environ.get('PYTHON','python3'),'tests/media_lifecycle_fault.py']),
                     ('media_redraw',[os.environ.get('PYTHON','python3'),'tests/media_redraw_measure.py','--verify'])]:
    begin=time.monotonic()
    with (out/f'{name}.log').open('w') as log:
        result=subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT)
        log.write(f'\nexit: {result.returncode}\n')
    results[name]={'exit':result.returncode,'seconds':round(time.monotonic()-begin,3)}
    print(('PASS' if result.returncode==0 else 'FAIL')+f': {name}, {out/name}.log',flush=True)
    if result.returncode:failed.append(name)
print('Leak detection '+('explicitly disabled' if args.disable_leaks else 'requested')+'.',flush=True)
report='\n'.join(['### Media sanitizer results',
    'LSan: '+('DISABLED — incomplete' if args.disable_leaks else 'enabled; fork cancellation does not check entire worker-body leaks'),
    *[f'- {name}: {"PASS" if r["exit"]==0 else "FAIL"} (exit {r["exit"]}, {r["seconds"]}s)' for name,r in results.items()],
    f'Total: {len(results)} required, {len(failed)} failed; {time.monotonic()-started:.3f}s'])+'\n'
(out/'summary.log').write_text(report)
(out/'summary.json').write_text(json.dumps({'complete':True,'leaks_enabled':not args.disable_leaks,'results':results},indent=2)+'\n')
print(report,flush=True)
if os.environ.get('GITHUB_STEP_SUMMARY'):
    with open(os.environ['GITHUB_STEP_SUMMARY'],'a') as summary:summary.write(report)
raise SystemExit(bool(failed))
