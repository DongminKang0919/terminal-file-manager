"""Build isolated ASan/UBSan/LSan binaries; never replace normal test binaries."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
import json
import time
import shlex
from pathlib import Path
import subprocess
import tempfile

p=argparse.ArgumentParser()
p.add_argument('--output-directory',type=Path,required=True)
p.add_argument('--disable-leaks',action='store_true',help='ASan/UBSan fallback when LSan is unsupported; not a leak check')
a=p.parse_args()
assert str(a.output_directory.resolve()).startswith('/tmp/tfile-sanitizers-')
a.output_directory.mkdir(exist_ok=True)
root=Path(__file__).resolve().parents[1]
core=[root/'src/model.c',*sorted((root/'src/core').glob('*.c')),*sorted((root/'src/platform').glob('posix*.c'))]
ui=[*core,*sorted(x for x in (root/'src/ui').glob('*.c') if x.name!='main.c')]
flags=['-D_XOPEN_SOURCE=700','-O1','-g','-Wall','-Wextra','-Wpedantic','-std=c11',
       '-fno-omit-frame-pointer','-fsanitize=address,undefined','-fno-pie','-no-pie']
targets={
 'preview_async_test':(['platform_task_send','platform_reader_open','platform_reader_peek','platform_reader_line','platform_reader_changed','platform_directory_empty'],ui),
 'preview_worker_test':(['platform_task_send','platform_task_command'],[x for x in core if x.name!='preview_job.c']),
 'mutation_job_test':(['read','write','fstatat','renameat2','platform_task_send','platform_task_receive'],core),
 'mutation_worker_test':(['platform_task_send','platform_mutation_control'],[x for x in core if x.name!='mutation_job.c']),
 'read_job_test':(['platform_task_send','platform_directory_open','malloc'],core),
 'performance_test':(['text_copy'],core),
 'mark_policy_test':([],ui),
 'vim_test':(['platform_reader_peek'],core),
 'favorites_test':(['platform_favorites_write'],core),
 'filter_test':([],core),
 'batch_conflict_test':(['platform_move','platform_copy_progress'],core),
 'external_test':([],core),
 'trash_test':(['fsync','write','unlinkat','fstatfs','fchmod','mkdirat'],core),
 'panels_ui_test':(['app_init','app_refresh','platform_directory_open','platform_reader_line','qsort','newwin','delwin','input_key','input_wide','confirm','malloc','calloc','realloc','free'],ui),
 'batch_test':(['malloc','calloc','realloc','platform_move'],core),
 'batch_ui_test':(['app_refresh','platform_directory_open','platform_reader_line','qsort','core_monotonic_ms','wrefresh','input_key','input_wide','batch_prepare','newwin','delwin','malloc','calloc','realloc'],ui),
 'operations_test':(['renameat2'],core),
 'sort_test':([],core), 'progress_test':([],core),
 'search_test':(['lstat','platform_directory_open','platform_directory_next'],core),
 'controller_test':(['app_refresh','confirm','run_file_operation'],ui),
 'history_ui_test':(['platform_directory_open'],ui),
 'keyboard_ui_test':(['input_wide','input_key','app_refresh','newwin','delwin'],ui),
 'popup_style_test':([],ui),
 'progress_ui_test':(['core_monotonic_ms','input_wide','wrefresh'],ui),
 'preview_test':(['platform_reader_open','platform_reader_line','platform_reader_close',
                 'platform_reader_changed','readdir','platform_directory_empty','platform_directory_open','platform_info','app_refresh'],ui),
 'text_test':([],[root/'src/ui/text.c']),
 'text_window_test':([],[root/'src/ui/text.c',root/'src/ui/text_window.c']),
}
for name in ['startup_ui_test','search_progress_ui_test','ownership_test']:
 if (root/'tests'/f'{name}.c').exists():
  wraps={'startup_ui_test':['platform_directory_open','qsort','app_init','input_key','initscr','terminal_session_check'],
         'search_progress_ui_test':['platform_monotonic_ms','core_monotonic_ms','input_key','input_wide','wrefresh'],
         'ownership_test':['malloc','calloc','realloc','free']}[name]
  targets[name]=(wraps,ui if name!='ownership_test' else core)
started=time.monotonic()
results={}
def build(item):
 begin=time.monotonic()
 name,(wraps,sources)=item
 cmd=['cc',*flags,'-o',str(a.output_directory/name),str(root/'tests'/f'{name}.c'),*map(str,sources)]
 if name=='operations_test': cmd+=['-DTFILE_TEST_HOOKS']
 if name=='trash_test': cmd+=['-DTFILE_TRASH_TEST_HOOKS']
 if sources==ui or name=='text_window_test': cmd+=['-lncursesw']
 if wraps: cmd+=['-Wl,'+','.join('--wrap='+x for x in wraps)]
 r=subprocess.run(cmd,text=True,capture_output=True)
 (a.output_directory/f'{name}.build.log').write_text('$ '+shlex.join(cmd)+'\n'+r.stdout+r.stderr+f'\nexit: {r.returncode}\n')
 return name,{'build_exit':r.returncode,'build_seconds':round(time.monotonic()-begin,3)}
with ThreadPoolExecutor(max_workers=3) as pool:
 for name,result in pool.map(build,targets.items()):
  results[name]=result
  print(f'{name}: build {"PASS" if result["build_exit"]==0 else "FAIL"} (exit {result["build_exit"]})',flush=True)
env={**os.environ,'ASAN_OPTIONS':f'detect_leaks={0 if a.disable_leaks else 1}:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1',
     'TERM':'xterm-256color','LC_ALL':'C.UTF-8'}
failed=[]
for name in targets:
 if results[name]['build_exit']:
  failed.append(name)
  results[name]['status']='FAIL (build; not run)'
  (a.output_directory/f'{name}.run.log').write_text(f'NOT RUN: build failed (exit {results[name]["build_exit"]})\n')
  continue
 begin=time.monotonic()
 binary=str(a.output_directory/name)
 if name=='operations_test':
  cmd=['python3',str(root/'tests/run_operations.py')]
  case_env={**env,'TFILE_OPERATIONS_TEST':binary}
 elif name=='vim_test':
  cmd=['python3',str(root/'tests/vim_tools.py')]
  case_env={**env,'TFILE_VIM_TEST':binary}
 elif name=='external_test':
  cmd=['python3',str(root/'tests/external_tools.py')]
  case_env={**env,'TFILE_EXTERNAL_TEST':binary}
 elif name in ['search_test','controller_test']:
  variables={'search_test':'TFILE_SEARCH_TEST','controller_test':'TFILE_CONTROLLER_TEST'}
  target='search' if name=='search_test' else 'controller'
  cmd=['python3',str(root/'tests/run_regressions.py'),target]
  case_env={**env,variables[name]:binary}
 else: cmd=[binary]; case_env=env
 with tempfile.TemporaryDirectory(prefix='tfile-native-san-home-') as home:
  case_env={**case_env,'HOME':home,'XDG_CONFIG_HOME':home+'/config','XDG_DATA_HOME':home+'/data','XDG_CACHE_HOME':home+'/cache'}
  r=subprocess.run(cmd,cwd=root,env=case_env,text=True,capture_output=True)
 results[name].update(run_exit=r.returncode,run_seconds=round(time.monotonic()-begin,3),status='FAIL' if r.returncode else 'PASS')
 (a.output_directory/f'{name}.run.log').write_text('$ '+shlex.join(cmd)+'\n'+r.stdout+r.stderr+f'\nexit: {r.returncode}\n')
 print(f'{name}: {"PASS" if not r.returncode else "FAIL"} (exit {r.returncode})',flush=True)
 if r.returncode: failed.append(name)
print('Leak detection disabled: these results do not verify leaks.' if a.disable_leaks else 'Leak detection requested and enabled; inspect logs for runtime limitations. No suppressions are used.')
summary={'leaks_enabled':not a.disable_leaks,'elapsed_seconds':round(time.monotonic()-started,3),'required':list(targets),'results':results}
(a.output_directory/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
lines=['### Native sanitizer results', 'LSan: '+('DISABLED — leak checks incomplete' if a.disable_leaks else 'enabled (fork tests cover parent; body harnesses cover normal teardown)'), *[f'- {name}: {results[name]["status"]} (build exit {results[name]["build_exit"]}, run exit {results[name].get("run_exit", "not run")})' for name in targets], f'Total: {len(targets)} required, {len(targets)-len(failed)} passed, {len(failed)} failed; {summary["elapsed_seconds"]}s']
report='\n'.join(lines)+'\n'
(a.output_directory/'summary.log').write_text(report)
print(report,flush=True)
if os.environ.get('GITHUB_STEP_SUMMARY'):
 with open(os.environ['GITHUB_STEP_SUMMARY'],'a') as output: output.write(report)
raise SystemExit(bool(failed))
