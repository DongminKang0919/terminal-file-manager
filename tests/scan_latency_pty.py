"""Hold a directory open indefinitely; input must work before the hold is released."""
import os
from pathlib import Path
import select
import signal
import sys
import tempfile
import time
import display_pty

baseline = '--baseline' in sys.argv
display_pty.BINARY = os.path.abspath('/tmp/tfile-scan-baseline' if baseline else
                                  os.environ.get('TFILE_READ_BINARY', 'tests/tfile_read'))

def pump(t, duration=.02):
    for fd in select.select([t.master], [], [], duration)[0]:
        try:
            data = os.read(fd, 65536)
        except OSError:
            return
        if data:
            t.screen.feed(data)

def until(t, condition, timeout=3):
    start = time.monotonic()
    while not condition():
        assert time.monotonic() - start < timeout, '\n'.join(t.screen.row(i) for i in range(t.screen.height))
        pump(t)
    return time.monotonic() - start

def text(t):
    return '\n'.join(t.screen.row(i) for i in range(t.screen.height))

for width, height in [(50, 9), (100, 24)]:
    for kind, phase in [('search', 'open'), ('directory', 'open')] + ([] if baseline else [('search', 'next'), ('directory', 'next')]):
        for action in (('esc',) if baseline else ('esc', 'mouse', 'resize', 'term')):
            with tempfile.TemporaryDirectory(prefix='tfile-read-latency-') as temp:
                root = Path(temp) / 'root'; root.mkdir()
                nested = root / 'nested'; nested.mkdir()
                (nested / 'result').write_text('new list')
                (root / 'old').write_text('previous list')
                if phase == 'next':
                    for i in range(130):
                        (root / f'old-{i:03}').touch()
                        (nested / f'result-{i:03}').touch()
                control = Path(temp) / 'hold'; marker = Path(str(control) + '.entered')
                os.environ['TFILE_SCAN_GATE'] = str(control)
                os.environ['TFILE_SCAN_PHASE'] = phase
                t = display_pty.Terminal(str(root), width, height)
                try:
                    control.write_text(str(root if kind == 'search' else nested))
                    os.write(t.master, ('\x1bORresult\n' if kind == 'search' else '\n').encode())
                    until(t, lambda: marker.exists() and bool(marker.read_text().strip()))
                    worker = int(marker.read_text())
                    if baseline:
                        # The old UI cannot produce any update during this held call.
                        pump(t, .1)
                        os.write(t.master, b'\x1b')
                        held = time.monotonic()
                        while time.monotonic() - held < .8:
                            pump(t)
                        assert 'Cancelled:' not in text(t)
                        control.unlink()
                        until(t, lambda: 'Cancelled:' in text(t) if kind == 'search' else 'result' in text(t))
                        print(f'baseline {kind} {width}x{height}: input blocked for at least 800 ms', flush=True)
                        if kind == 'search': t.send('\x1b')
                        continue
                    assert worker != t.proc.pid
                    until(t, lambda: ('Searching' if kind == 'search' else 'Reading directory') in text(t))
                    started = time.monotonic()
                    if action == 'term':
                        t.proc.send_signal(signal.SIGTERM)
                        assert t.proc.wait(timeout=.6) == 0
                    elif action == 'resize':
                        import fcntl, struct, termios
                        fcntl.ioctl(t.master, termios.TIOCSWINSZ, struct.pack('HHHH', 10, 52, 0, 0))
                        t.screen = display_pty.Screen(10, 52)
                        t.proc.send_signal(signal.SIGWINCH)
                        until(t, lambda: 'Enter: Open' in text(t), .6)
                    else:
                        if action == 'esc': os.write(t.master, b'\x1b')
                        else:
                            rw = min(90, width - 8) if kind == 'search' else min(60, width - 4)
                            rx = (width - rw) // 2
                            y = height - 3 if kind == 'search' else (height - 7) // 2 + 5
                            os.write(t.master, f'\x1b[<0;{rx+4};{y+1}M'.encode())
                        until(t, lambda: ('Cancelled:' if kind == 'search' else 'Open directory: Directory load') in text(t), .6)
                    latency = (time.monotonic() - started) * 1000
                    assert control.exists(), 'test released the held read prematurely'
                    if action != 'term':
                        until(t, lambda: not Path(f'/proc/{worker}').exists(), .6)
                        if kind == 'search' and action != 'resize': t.send('\x1b')
                        assert 'old' in text(t) and 'result' not in text(t), text(t)
                        # A later load must succeed; no late old result may replace it.
                        control.unlink(); marker.unlink()
                        t.send('\n')
                        until(t, lambda: 'result' in text(t))
                    else:
                        deadline = time.monotonic() + .6
                        while Path(f'/proc/{worker}').exists() and time.monotonic() < deadline:
                            time.sleep(.01)
                        assert not Path(f'/proc/{worker}').exists()
                    print(f'{kind}/{phase} {action} {width}x{height}: {latency:.1f} ms, held read cancelled and child reaped', flush=True)
                finally:
                    control.unlink(missing_ok=True)
                    if t.proc.poll() is None: t.close()
                    else: os.close(t.master)
                    os.environ.pop('TFILE_SCAN_GATE', None)
                    os.environ.pop('TFILE_SCAN_PHASE', None)
