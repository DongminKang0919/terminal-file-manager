"""Mutation control: suppress app process-group SIGKILL, keep direct kills.
The real gated descendant regression must FAIL; Python cleanup is not injected.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

binary=Path(os.environ.get('TFILE_BINARY','./tfile')).resolve()
with tempfile.TemporaryDirectory(prefix='tfile-media-fault-') as directory:
    base=Path(directory);source=base/'fault.c';library=base/'fault.so';launcher=base/'launch'
    source.write_text('''#define _GNU_SOURCE
#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>
int kill(pid_t pid, int sig) {
    if(pid<0 && sig==SIGKILL)return 0;
    return (int)syscall(SYS_kill,pid,sig);
}
''')
    subprocess.run([os.environ.get('CC','cc'),'-shared','-fPIC','-o',str(library),str(source)],check=True)
    # ASan's runtime must load first. The interposer is only in the actual UI
    # and its fork/exec descendants, never in the supervising Python test.
    preload=str(library)
    if 'detect_leaks' in os.environ.get('ASAN_OPTIONS',''):
        asan=subprocess.check_output([os.environ.get('CC','cc'),'-print-file-name=libasan.so'],text=True).strip()
        preload=asan+':'+preload
    launcher.write_text('#!/bin/sh\nexec /usr/bin/env LD_PRELOAD='+shlex.quote(preload)+' '+shlex.quote(str(binary))+' "$@"\n')
    launcher.chmod(0o700)
    legacy=subprocess.run(['/usr/bin/python3','tests/media_lifecycle_pty.py','--case','descendant-exit','--legacy-absence'],
        env={**os.environ,'TFILE_BINARY':str(binary)},stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=15)
    print(legacy.stdout,flush=True)
    assert legacy.returncode!=0 and 'legacy immediate PID absence rejects stopped process' in legacy.stdout
    print('PASS: unmodified cleanup FAILS legacy PID absence assertion; stopped/reaped policy accepts same process structure',flush=True)
    result=subprocess.run(['/usr/bin/python3','tests/media_lifecycle_pty.py','--case','descendant-exit'],
        env={**os.environ,'TFILE_BINARY':str(launcher)},stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=15)
    print(result.stdout,flush=True)
    assert result.returncode!=0, 'regression accepted broken group cleanup'
    assert 'process still executing after cleanup deadline' in result.stdout, result.returncode
    print('PASS: deliberate missing group kill causes gated descendant regression to FAIL (live S state); no retries')
