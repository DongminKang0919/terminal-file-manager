"""CLI protocol plus real interactive '--' paths, isolated from user settings."""
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import stat
import struct
import subprocess
import tempfile
import termios
import time
from display_pty import Screen

BINARY = os.path.abspath(os.environ.get('TFILE_BINARY', './tfile'))
runtime = {k:os.environ[k] for k in ('ASAN_OPTIONS','UBSAN_OPTIONS','LSAN_OPTIONS') if k in os.environ}
version_file = Path(__file__).resolve().parents[1] / 'src/version.h'
# Installed tests receive the expected version before the source is removed.
version = os.environ.get('TFILE_EXPECTED_VERSION')
if version is None:
    version = re.search(r'#define TFILE_VERSION "([^"]+)"', version_file.read_text())[1]


def run(*args, **kwargs):
    result=subprocess.run(*args, **kwargs)
    assert not re.search(rb'(?:AddressSanitizer|LeakSanitizer|runtime error:)',(result.stdout or b'')+(result.stderr or b'')),result
    return result


def snapshot(root):
    return [(str(p.relative_to(root)), stat.S_IFMT(p.lstat().st_mode), p.lstat().st_mtime_ns,
             p.read_bytes() if p.is_file() else None) for p in sorted(root.rglob('*'))]


def interactive(base, args, directory):
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH',24,100,0,0))
    before = termios.tcgetattr(slave)
    env = {**runtime, 'PATH':'', 'TERM':'xterm-256color', 'LC_ALL':'C.UTF-8',
           'HOME':str(base/'interactive-home'), 'XDG_CONFIG_HOME':str(base/'interactive-config')}
    process = subprocess.Popen([BINARY]+args, cwd=base, stdin=slave, stdout=slave, stderr=slave, env=env)
    screen = Screen(24,100)
    try:
        deadline = time.monotonic()+3
        while 'Shown:' not in screen.row(22):
            assert time.monotonic()<deadline, ('CLI interactive startup',args,process.poll())
            if select.select([master],[],[],.02)[0]:screen.feed(os.read(master,65536))
        assert str(directory) in screen.row(1), (args,screen.row(1))
        os.write(master,b'q')
        assert process.wait(timeout=3)==0
        assert termios.tcgetattr(slave)==before
        assert not (base/'interactive-config').exists()
    finally:
        if process.poll() is None:process.kill();process.wait()
        os.close(master);os.close(slave)


with tempfile.TemporaryDirectory(prefix='tfile-cli-') as d:
    base=Path(d);config=base/'config/tfile';config.mkdir(parents=True)
    os.mkfifo(config/'settings.conf')  # Must never be consulted by informational options.
    (config/'favorites.conf').write_text('preserve me\n')
    initial=snapshot(base)
    help_text=None
    for term in [None,'','dumb','not-a-valid-terminal']:
        for home in [False,True]:
            env={**runtime,'PATH':''}
            if term is not None:env['TERM']=term
            if home:env.update(HOME=str(base/'home'),XDG_CONFIG_HOME=str(base/'config'),
                               XDG_DATA_HOME=str(base/'data'),XDG_CACHE_HOME=str(base/'cache'))
            for option in ['--help','-h','--version']:
                result=run([BINARY,option],stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE,env=env,timeout=3)
                assert result.returncode==0 and not result.stderr,(option,term,home,result)
                assert result.stdout and all(c>=32 or c==10 for c in result.stdout),result.stdout
                if option=='--version':assert result.stdout==f'tfile {version}\n'.encode(),result.stdout
                else:
                    if help_text is None:help_text=result.stdout
                    assert result.stdout==help_text
                    for word in [b'Usage:',b'--help',b'-h',b'--version',b'--',b'current working directory',
                                 b'TTY',b'TERM',b'F1',b'F9',b'https://github.com/DongminKang0919/terminal-file-manager']:
                        assert word in result.stdout,word
            assert snapshot(base)==initial
    for args in [['--bad'],['-'],['--help=x'],['-hv'],['a','b'],['--help','a'],['a','--help'],
                 ['--version','a'],['--help','--version'],['-h','-h'],['--help','--'],['--','a','b']]:
        r=run([BINARY]+args,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE,env={**runtime,'PATH':''},timeout=3)
        assert r.returncode==1 and not r.stdout and b'tfile --help' in r.stderr,(args,r)
        assert b'\x1b' not in r.stderr
    for args in [[],['.'],['--'],['--','-h'],['--','--version']]:
        r=run([BINARY]+args,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE,env={**runtime,'PATH':''},timeout=3)
        assert r.returncode==1 and b'interactive terminal' in r.stderr and not r.stdout,(args,r)
    # stdin and stdout are actual pipes, with nonempty stdin unrelated to CLI.
    read_fd,write_fd=os.pipe();os.write(write_fd,b'ignored input\n');os.close(write_fd)
    try:
        r=run([BINARY,'--help'],stdin=read_fd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env={**runtime,'PATH':''},timeout=3)
        assert r.returncode==0 and r.stdout==help_text and not r.stderr
    finally:os.close(read_fd)
    for option in ['--help','-h','--version']:
        with tempfile.TemporaryFile() as output:
            r=run([BINARY,option],stdin=subprocess.DEVNULL,stdout=output,
                  stderr=subprocess.PIPE,env={**runtime,'PATH':''},timeout=3)
            output.seek(0)
            expected=f'tfile {version}\n'.encode() if option=='--version' else help_text
            assert r.returncode==0 and not r.stderr and output.read()==expected
    assert snapshot(base)==initial
    # Real TTY proves '--' is routed to directory startup rather than just passing
    # argument parsing and failing an unrelated terminal check.
    for name in ['-folder','--help','--version','--','-']:
        directory=base/name;directory.mkdir();(directory/'sample').write_text('safe\n')
        interactive(base,['--',name],directory)
    interactive(base,[],base)
    interactive(base,['--'],base)
    interactive(base,['./-folder'],base/'-folder')
print('PASS: CLI output/status matrix, no settings changes/control bytes, pipes/absent environment, '
      'invalid combinations, real TTY -- paths/current directory and terminal restoration')
