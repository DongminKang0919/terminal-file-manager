"""New errors must not be hidden by retained success; nested resize cancels all."""
import fcntl
import os
from pathlib import Path
import signal
import struct
import tempfile
import termios
from display_pty import Terminal

HOME='\x1bOH'; DOWN='\x1bOB'; DELETE='\x1b[3~'; F2='\x1bOQ'; F3='\x1bOR'; F5='\x1b[15~'; F6='\x1b[17~'; F9='\x1b[20~'
failures=[]
def check(condition,label):
    print(('PASS: ' if condition else 'FAIL: ')+label,flush=True)
    if not condition:failures.append(label)
for w,h in [(50,9),(80,24),(160,32)]:
    with tempfile.TemporaryDirectory(prefix='tfile-recovery-') as directory:
        root=Path(directory);locked=root/'locked';locked.mkdir();(root/'source').write_text('original')
        t=Terminal(directory,w,h)
        def body():return '\n'.join(t.screen.row(y) for y in range(h))
        def status():return t.screen.row(h-2)
        def find(name):t.send('\x06'+name+'\n')
        def replace(value):t.send(HOME+DELETE*600+str(value))
        def menu(n):t.send(F9+HOME+DOWN*n+'\n')
        def retained_success():
            t.send('!');check('File operation: Success' in body(),f'{w}x{h} prior file result retained');t.send('\x1b')
        def prior_success():t.send(F2+'created\n');assert (root/'created').exists()
        try:
            prior_success();find('locked')
            if os.geteuid()!=0:
                locked.chmod(0)
                try:
                    t.send('\n');check('Open directory:' in status() and '[Success]' not in status(),f'{w}x{h} denied navigation overrides old success')
                    retained_success()
                    t.send(F3+'unmatched\n');assert 'Incomplete:' in body();t.send('\x1b')
                    check('Search incomplete' in status() and '[Success]' not in status(),f'{w}x{h} incomplete search overrides old success')
                    retained_success()
                    # Collected results from incomplete search are still openable.
                    t.send(F3+'source\n');assert 'Incomplete:' in body();t.send('\n');assert 'Search' not in t.screen.row(1)
                    root.chmod(0);t.send('r')
                    check('Refresh failed:' in status() and '[Success]' not in status(),f'{w}x{h} failed refresh overrides old success')
                finally:root.chmod(0o700);locked.chmod(0o700)
            else:print('SKIP: real permission denial requires non-root',flush=True)
            t.send('r');find('locked');t.send('\n');t.send('\x7f');locked.rmdir()
            t.send(']');check('Forward:' in status() and '[Success]' not in status(),f'{w}x{h} missing history location overrides old success')
            retained_success()
            # Empty dual source has no selectable Source picker: don't present a
            # fictitious one-item form or replace the latest file result.
            locked.mkdir();find('locked');t.send('\n');menu(18)
            for key in [F5,F6]:
                t.send(key);check('No transfer targets' in status() and ' now ]' not in body(),f'{w}x{h} empty dual transfer stays in list')
                if ' now ]' in body():t.send('\x1b')
            retained_success();t.send('\x7f');find('source')
            # All nesting levels cancel on a resize event even if terminal size
            # returns to its previous dimensions before ncurses consumes it.
            t.send(F5+'\t\t\t\n');assert 'Transfer paths' in body()
            fcntl.ioctl(t.master,termios.TIOCSWINSZ,struct.pack('HHHH',h,w,0,0));os.kill(t.proc.pid,signal.SIGWINCH);t.read()
            check('Transfer paths' not in body() and ' now ]' not in body(),f'{w}x{h} Paths resize cancels parent transfer')
            if ' now ]' in body():t.send('\x1b')
            assert (root/'source').read_text()=='original'
            # Source removed while the form is open: retain the destination/name
            # in the legacy single-list form and do not create the requested copy.
            menu(17);t.send(F5);(root/'source').unlink();replace(root);t.send('\n');replace('copy-source');t.send('\n\n')
            assert 'Copy' in body() and 'copy-source' in body() and not (root/'copy-source').exists();t.send('\x1b')
            # Unreadable source after opening: engine rejects the copy, the form
            # keeps its destination/name, and retry works after permission repair.
            private=root/'private';private.write_text('private data');t.send('r');find('private')
            t.send(F5);replace(root);t.send('\n');replace('copy-private');t.send('\n')
            if os.geteuid()!=0:
                private.chmod(0)
                try:
                    t.send('\n');assert 'Permission denied' in body() and not (root/'copy-private').exists()
                finally:private.chmod(0o600)
                t.send('\n')
            else:t.send('\n')
            assert (root/'copy-private').read_text()=='private data' and private.read_text()=='private data'
            # Same directory/name and own-descendant transfer use the existing
            # no-overwrite/self-transfer policy, preserve originals, and cancel.
            find('private');t.send(F5+'\n\n\n');assert 'exists' in body();t.send('\x1b')
            child=locked/'child';child.mkdir();t.send('r');find('locked')
            for key in [F5,F6]:
                t.send(key);replace(child);t.send('\n\n\n')
                assert 'Cannot copy or move' in body() and list(child.iterdir())==[];t.send('\x1b')

        finally:root.chmod(0o700);t.close()
assert not failures,failures
print('PASS: recovery flows and prior result preservation at all sizes')
