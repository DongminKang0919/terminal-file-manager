"""Synthetic one-second filesystem stalls; all fixtures are disposable."""
import os
from pathlib import Path
import select
import sys
import tempfile
import time
import display_pty

baseline='--baseline' in sys.argv
display_pty.BINARY=os.path.abspath('/tmp/tfile-preview-before' if baseline else os.environ.get('TFILE_PREVIEW_BINARY','tests/tfile_preview'))
def pump(t,wait=.01):
    if select.select([t.master],[],[],wait)[0]:
        try: t.screen.feed(os.read(t.master,65536))
        except OSError: pass
def until(t,fn,timeout=3):
    start=time.monotonic()
    while not fn():
        assert time.monotonic()-start<timeout, text(t)
        pump(t)
    return (time.monotonic()-start)*1000
def text(t): return '\n'.join(t.screen.row(i) for i in range(t.screen.height))
if __name__=='__main__':
    for size in [(50,9),(100,24)]:
        for phase in ['open','peek','directory','line','metadata','page']:
            with tempfile.TemporaryDirectory(prefix='preview-latency-') as temp:
                root=Path(temp)/'root'; root.mkdir()
                (root/'a-dir').mkdir(); (root/'b-file').write_text('old-content\n'*1000)
                (root/'c-file').write_text('new-content\n')
                gate=Path(temp)/'gate'; marker=Path(str(gate)+'.entered')
                os.environ['TFILE_PREVIEW_GATE']=str(gate); os.environ['TFILE_PREVIEW_PHASE']='line' if phase=='page' else phase
                t=display_pty.Terminal(str(root),*size)
                try:
                    if phase=='page':
                        t.send('j'); until(t,lambda:'old-content' in text(t))
                        t.send('\n')
                    gate.touch()
                    # Initial selection is directory; regular file follows.
                    if phase=='page': os.write(t.master,b'\x1b[6~')
                    elif phase!='directory': os.write(t.master,b'j')
                    else: os.write(t.master,b'r')
                    until(t,lambda:marker.exists() and marker.read_text().strip())
                    pid=int(marker.read_text())
                    gate.unlink() # Keep the current one-second call held; later calls run normally.
                    start=time.monotonic(); os.write(t.master,b'jq')
                    while t.proc.poll() is None and time.monotonic()-start<2:
                        pump(t)
                    assert t.proc.poll()==0, 'quit blocked >2 seconds'
                    latency=(time.monotonic()-start)*1000
                    assert (latency>=750 and pid==t.proc.pid) if baseline else (latency<600 and pid!=t.proc.pid), (phase,latency,pid)
                    print(f'{"before" if baseline else "after"} {phase} {size[0]}x{size[1]} navigation+quit: {latency:.1f} ms',flush=True)
                finally:
                    gate.unlink(missing_ok=True)
                    if t.proc.poll() is None:t.close()
                    else:os.close(t.master)
                    os.environ.pop('TFILE_PREVIEW_GATE',None);os.environ.pop('TFILE_PREVIEW_PHASE',None)
