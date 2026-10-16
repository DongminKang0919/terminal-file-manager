"""Event-gated Linux media lifetime checks; no real pixel-display claims.

This test is a subreaper ONLY to collect converter orphans itself. The app is
unchanged: worker waitpid is its responsibility, converter adoption is the host's.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
from media_pty import Terminal, GraphicsScreen, assert_artifacts_removed
from process_probe import process_state, wait_stopped

class LifecycleScreen(GraphicsScreen):
    def csi(self, params, command):
        # Editor return explicitly resets xterm modifyOtherKeys. It paints no
        # cells; retain every graphics ordering/damage check from the base.
        if params == '>4;0' and command == 'm': return
        super().csi(params, command)


LIBC = ctypes.CDLL(None, use_errno=True)
assert LIBC.prctl(36, 1, 0, 0, 0) == 0  # PR_SET_CHILD_SUBREAPER (test only)
STUB = r'''#!/usr/bin/python3
import json, os, signal, sys, time
from pathlib import Path
log=os.environ['TFILE_MEDIA_TEST_LOG']
def event(name):
 record=dict(event=name,start=int(Path('/proc/self/stat').read_text().rsplit(')',1)[1].split()[19]),seconds=time.monotonic(),pid=os.getpid(),ppid=os.getppid(),pgid=os.getpgrp(),temporary=os.environ['MAGICK_TEMPORARY_PATH'])
 fd=os.open(log,os.O_WRONLY|os.O_CREAT|os.O_APPEND,0o600)
 os.write(fd,(json.dumps(record)+'\n').encode());os.close(fd)
signal.signal(signal.SIGTERM,signal.SIG_IGN)
if os.environ.get('TFILE_MEDIA_DESCENDANT')=='1':
 r,w=os.pipe()
 child=os.fork()
 if child==0:
  os.close(r);event('descendant');os.write(w,b'1');os.close(w)
  with open(os.environ['TFILE_MEDIA_GATE'],'rb',buffering=0) as gate:gate.read(1)
  event('descendant-return');os._exit(0)
 os.close(w);assert os.read(r,1)==b'1';os.close(r)
event('started')
with open(os.environ['TFILE_MEDIA_GATE'],'rb',buffering=0) as gate:gate.read(1)
event('before-return')
sys.stdout.buffer.write(b'\x1bP0;0;0q"1;1;6;6#0;2;100;0;0!6~\x1b\\');sys.stdout.buffer.flush()
event('returned')
'''


def records(log):
    if not log.exists(): return []
    return [json.loads(line) for line in log.read_text().splitlines()]


def collect(identity):
    state=wait_stopped(identity)
    deadline=time.monotonic()+2
    previous=None
    while state is not None and state['start']==identity['start']:
        if state!=previous:
            print('ADOPTION STATE',json.dumps(state),flush=True);previous=state
        # A killed parent can still own a zombie while it finishes do_exit.
        # Wait for actual adoption, never confuse ECHILD with disappearance.
        try:
            pid,status=os.waitpid(identity['pid'],os.WNOHANG)
        except ChildProcessError:
            pid=0
        if pid:
            print('ADOPTED REAP',pid,status,flush=True)
            break
        assert time.monotonic()<deadline,('orphan not adopted/reaped',identity,state)
        time.sleep(.005)
        state=process_state(identity['pid'])
    state=process_state(identity['pid'])
    assert state is None or state['start']!=identity['start'],state


def scenario(base, name, descendant=False):
    case=base/name;case.mkdir();root=case/'files';root.mkdir();tools=case/'tools';tools.mkdir()
    (root/'a.png').write_bytes(b'\x89PNG\r\n\x1a\ngated')
    (root/'b.txt').write_text('safe next selection\n')
    (tools/'magick').write_text(STUB);(tools/'magick').chmod(0o700)
    # Vim uses the cursor text file, while a deliberately gated image remains
    # active until the selection change cancels it. Real Vim handoff is covered
    # separately by vim_pty; this stub records handoff and returns normally.
    (tools/'vim').write_text('#!/usr/bin/python3\n');(tools/'vim').chmod(0o700)
    gate=case/'gate';os.mkfifo(gate);fd=os.open(gate,os.O_RDWR|os.O_NONBLOCK)
    log=case/'events';os.environ.update(TFILE_MEDIA_GATE=str(gate),TFILE_MEDIA_DESCENDANT=str(int(descendant)))
    t=Terminal(root,tools,log);t.screen=LifecycleScreen(24,100);identities=[];worker=None
    begin=time.monotonic()
    try:
        t.wait(lambda:any(x['event']=='started' for x in records(log)))
        observed=records(log);print('EVENTS',name,json.dumps(observed),flush=True)
        identities=[process_state(x['pid']) for x in observed];assert all(identities)
        worker=process_state(next(x['ppid'] for x in observed if x['event']=='started'));assert worker
        assert all(x['pgid']==worker['pid'] for x in identities)
        print('RUNNING',name,json.dumps(dict(ui=process_state(t.proc.pid),worker=worker,converters=identities)),flush=True)
        assert all(Path(x['temporary']).is_dir() for x in observed)
        if name.startswith('complete'):
            if name!='complete-visible':
                os.kill(worker['pid'],signal.SIGSTOP)
                t.wait(lambda:process_state(worker['pid'])['state']=='T')
            os.write(fd,b'x'*(2 if descendant else 1))
            t.wait(lambda:any(x['event']=='returned' for x in records(log)))
            if name=='complete-visible':t.wait(lambda:t.screen.image is not None)
            else:
                t.wait(lambda:process_state(identities[-1]['pid'])['state']=='Z')
                assert all(Path(x['temporary']).is_dir() for x in observed)
                print('BOUNDARY: converter exited; stopped worker has not reaped it',flush=True)
                if name=='complete-popup':t.send('\x1bOP');t.send('\x1b')
                elif name=='complete-resize':t.resize(49,8)
                elif name=='complete-vim':
                    t.send('\x1bOB');t.wait(lambda:'safe next selection' in t.text());t.send('e')
        elif name=='popup':
            t.send('\x1bOP')
        elif name=='resize':
            t.resize(49,8)
        elif name in ('selection','vim'):
            t.send('\x1bOB')
            t.wait(lambda:'safe next selection' in t.text())
            if name=='vim':t.send('e')
        elif name=='off':
            t.send('\x1b[18~'+'\x1bOB'*5+'\n'+'\x1b')
        elif name=='stopped-converter':
            os.kill(identities[-1]['pid'],signal.SIGSTOP)
            t.wait(lambda:process_state(identities[-1]['pid'])['state']=='T')
        if name in ('selection','off','popup','resize','vim'):
            t.wait(lambda:process_state(worker['pid']) is None)
            for identity in identities:wait_stopped(identity)
            assert_artifacts_removed(log)
        if name=='signal':
            os.kill(t.proc.pid,signal.SIGTERM);t.wait(lambda:t.proc.poll() is not None)
            assert t.proc.returncode==0
        elif name=='popup':
            os.kill(t.proc.pid,signal.SIGHUP);t.wait(lambda:t.proc.poll() is not None)
            assert t.proc.returncode==0
        else:
            t.close()
        # Direct worker must be reaped by UI on these interruptible test paths.
        assert process_state(worker['pid']) is None,worker
        # Dismissing a popup may start a fresh request before q. Track every
        # converter actually launched, rather than only the original selection.
        known={x['pid'] for x in identities}
        for event in records(log):
            if event['pid'] not in known:
                identity=process_state(event['pid'])
                if identity is not None:
                    assert identity['start']==event['start']
                    identities.append(identity)
                known.add(event['pid'])
            if event['event']=='started':
                assert process_state(event['ppid']) is None, event
        states=[process_state(x['pid']) for x in identities]
        print('UI EXIT',name,json.dumps(states),flush=True)
        if any(x and x['state']=='Z' for x in states):
            print('OLD immediate /proc absence assertion FAILS: stopped orphan zombie; not executing',flush=True)
        if args.legacy_absence:
            assert not Path(f'/proc/{identities[-1]["pid"]}').exists(), 'legacy immediate PID absence rejects stopped process'
        for identity in identities:collect(identity)
        assert_artifacts_removed(log)
        assert (root/'a.png').read_bytes()==b'\x89PNG\r\n\x1a\ngated'
        assert (root/'b.txt').read_text()=='safe next selection\n'
        print('PASS',name,'descendant',descendant,round(time.monotonic()-begin,3),'seconds',flush=True)
    except BaseException:
        print('FAIL EVENTS',name,json.dumps(records(log)),flush=True)
        print('FAIL STATES',json.dumps([process_state(x['pid']) for x in identities]+[process_state(t.proc.pid)]),flush=True)
        raise
    finally:
        # Faults must not leave this test's processes or artifacts behind.
        if worker:
            try:os.killpg(worker['pid'],signal.SIGKILL)
            except ProcessLookupError:pass
        if t.proc.poll() is None:
            t.proc.kill();t.proc.wait()
        if t.master>=0:os.close(t.master);t.master=-1
        os.close(fd)
        for identity in identities:
            state=process_state(identity['pid'])
            if state is None or state['start']!=identity['start']:continue
            try:os.kill(identity['pid'],signal.SIGKILL)
            except ProcessLookupError:pass
            collect(identity)


parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--case', choices=['descendant-exit'])
parser.add_argument('--legacy-absence',action='store_true',help='Negative control: use the old immediate PID absence assertion')
args=parser.parse_args()

with tempfile.TemporaryDirectory(prefix='tfile-media-lifetime-') as directory:
    base=Path(directory);os.environ.update(HOME=str(base/'home'),XDG_CONFIG_HOME=str(base/'config'),
        XDG_DATA_HOME=str(base/'data'),XDG_CACHE_HOME=str(base/'cache'))
    # Negative control: a genuinely live converter is never accepted as stopped.
    p=subprocess.Popen(['/usr/bin/python3','-c','import signal; signal.pause()'])
    try:
        try:wait_stopped(process_state(p.pid),seconds=.05)
        except AssertionError:print('PASS negative control: live leftover rejected',flush=True)
        else:raise AssertionError('cleanup observer accepted live child')
    finally:p.kill();p.wait()
    initial_fds=len(list(Path('/proc/self/fd').iterdir()))
    if args.case:
        scenario(base,args.case,True)
        raise SystemExit(0)
    for name in ['exit','selection','off','popup','resize','vim','signal','stopped-converter','complete-before-reap','complete-visible','complete-popup','complete-resize','complete-vim']:
        scenario(base,name)
    scenario(base,'descendant-exit',True)
    scenario(base,'complete-descendant',True)
    # Fixed count and total deadline, no retry-on-failure.
    start=time.monotonic()
    for i in range(6):
        assert time.monotonic()-start<30
        scenario(base,'repeat-'+str(i),True)
    assert len(list(Path('/proc/self/fd').iterdir()))==initial_fds
    try:pid,status=os.waitpid(-1,os.WNOHANG)
    except ChildProcessError:pass
    else:raise AssertionError(('uncollected child',pid,status,process_state(pid)))
print('PASS: 21 event-gated cases; worker reaped, converters stopped/adopted/reaped, private artifacts removed, source unchanged; no FD/child accumulation')
