"""ASan/UBSan for terminal parsing, filtered input and the real diagnostic UI."""
import argparse
import os
import json
import time
from pathlib import Path
import subprocess
import tempfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-directory',default='/tmp/tfile-terminal-sanitizers')
parser.add_argument('--disable-leaks',action='store_true')
args=parser.parse_args()
root=Path(__file__).resolve().parents[1];os.chdir(root)
out=Path(args.output_directory).resolve();out.mkdir(parents=True,exist_ok=True)
# Reusing an output directory must never leave a previous successful summary
# looking current after a compiler failure.
(out/'summary.log').write_text('INCOMPLETE: building required sanitized binaries; runtime tests not yet run.\n')
(out/'summary.json').write_text(json.dumps({'complete':False,'leaks_enabled':not args.disable_leaks})+'\n')
core=['src/model.c']+sorted(str(p) for p in Path('src/core').glob('*.c'))+sorted(str(p) for p in Path('src/platform').glob('posix*.c'))
ui=sorted(str(p) for p in Path('src/ui').glob('*.c') if p.name!='main.c')
flags=[os.environ.get('CC','cc'),'-D_XOPEN_SOURCE=700','-std=c11','-O1','-g','-Wall','-Wextra','-Wpedantic',
       '-fsanitize=address,undefined','-fno-omit-frame-pointer']
started=time.monotonic()
results={}
for name,test,sources,extra in [
    ('tfile_ui_io','tests/ui_io_gate.c',core+ui+['src/ui/main.c'],['-lncursesw','-Wl,--wrap=core_resolve_directory,--wrap=platform_directory_open,--wrap=platform_create,--wrap=platform_create_progress,--wrap=platform_media_open,--wrap=open,--wrap=access,--wrap=pwrite,--wrap=core_read_poll,--wrap=platform_directory_next,--wrap=platform_resolve']),
    ('terminal_test','tests/terminal_test.c',core,[]),
    ('terminal_input_test','tests/terminal_input_test.c',core+ui,['-lncursesw','-Wl,--wrap=core_monotonic_ms']),
    ('graphics_probe_test','tests/graphics_probe_test.c',core+ui,['-lncursesw','-Wl,--wrap=core_monotonic_ms,--wrap=core_terminal_pixels,--wrap=terminal_query,--wrap=terminal_query_cells']),
    ('tfile','src/ui/main.c',core+ui,['-lncursesw']),
    ('tfile_read','tests/scan_gate.c',core+ui+['src/ui/main.c'],['-lncursesw','-Wl,--wrap=platform_directory_open,--wrap=platform_directory_next'])]:
    with (out/(name+'.build.log')).open('w') as log:
        built=subprocess.run(flags+['-o',str(out/name),test]+sources+extra,stdout=log,stderr=subprocess.STDOUT)
    print(f'{name}: build {"PASS" if built.returncode==0 else "FAIL"} (exit {built.returncode})',flush=True)
    if built.returncode:
        (out/'summary.log').write_text(f'FAIL: build {name}, exit {built.returncode}; required runtime tests NOT RUN.\n')
        (out/'summary.json').write_text(json.dumps({'complete':False,'build_failed':name,'exit':built.returncode})+'\n')
        raise SystemExit(built.returncode)
with tempfile.TemporaryDirectory(prefix='tfile-terminal-san-home-') as home:
    env={**os.environ,'HOME':home,'XDG_CONFIG_HOME':home+'/config',
         'XDG_DATA_HOME':home+'/data','XDG_CACHE_HOME':home+'/cache',
         'ASAN_OPTIONS':f'detect_leaks={int(not args.disable_leaks)}:halt_on_error=1',
         'UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1','TFILE_BINARY':str(out/'tfile'),
         'TFILE_READ_BINARY':str(out/'tfile_read'),'TFILE_IO_BINARY':str(out/'tfile_ui_io')}
    for name,command in [('cli',['python3','tests/cli_test.py']),('ux_queued_browse_pty',['python3','tests/ux_queued_browse_pty.py']),('ux_targets_pty',['python3','tests/ux_targets_pty.py']),('startup_pty',['python3','tests/startup_latency_pty.py']),('ui_io_pty',['python3','tests/ui_io_latency_pty.py']),('ui_io_lifecycle_pty',['python3','tests/ui_io_lifecycle_pty.py']),('ui_io_normal_pty',['python3','tests/ui_io_normal_pty.py']),('terminal',[str(out/'terminal_test')]),('input',[str(out/'terminal_input_test')]),
                         ('probe',[str(out/'graphics_probe_test')]),('pty',['python3','tests/terminal_pty.py']),
                         ('auto_pty',['python3','tests/terminal_auto_pty.py']),
                         ('vim_pty',['python3','tests/vim_pty.py']),
                         ('editor_shutdown_pty',['python3','tests/editor_shutdown_pty.py']),
                         ('mark_discovery_pty',['python3','tests/mark_discovery_pty.py']),
                         ('reads_pty',['python3','tests/scan_latency_pty.py'])]:
        begin=time.monotonic()
        with (out/(name+'.log')).open('w') as log:
            result=subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT)
            log.write(f'\nexit: {result.returncode}\n')
        results[name]={'exit':result.returncode,'seconds':round(time.monotonic()-begin,3)}
        print(('PASS' if result.returncode==0 else 'FAIL')+f': {name}, exit {result.returncode}, {out/name}.log',flush=True)
print('LSan '+('disabled; incomplete' if args.disable_leaks else 'requested')+'.')

report='\n'.join(['### Startup, terminal and UI preparation sanitizer results',
    'LSan: '+('DISABLED — leak checks incomplete' if args.disable_leaks else 'enabled; fork worker bodies require separate harnesses'),
    *[f'- {name}: {"PASS" if r["exit"]==0 else "FAIL"} (exit {r["exit"]}, {r["seconds"]}s)' for name,r in results.items()],
    f'Total: {len(results)} required, {sum(r["exit"]!=0 for r in results.values())} failed; {time.monotonic()-started:.3f}s'])+'\n'
(out/'summary.log').write_text(report)
(out/'summary.json').write_text(json.dumps({'complete':True,'leaks_enabled':not args.disable_leaks,'results':results},indent=2)+'\n')
print(report,flush=True)
if os.environ.get('GITHUB_STEP_SUMMARY'):
    with open(os.environ['GITHUB_STEP_SUMMARY'],'a') as summary:summary.write(report)
raise SystemExit(any(r['exit']!=0 for r in results.values()))
