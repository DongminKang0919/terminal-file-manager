"""Runtime candidate creation versus startup dual initialization."""
import os
from pathlib import Path
import sys
import tempfile
import time
import display_pty
from preview_latency_pty import until,text,pump
baseline='--baseline' in sys.argv
display_pty.BINARY=os.path.abspath('/tmp/tfile-panels-before' if baseline else 'tests/tfile_read')
F9='\x1b[20~';HOME='\x1bOH';DOWN='\x1bOB'
for action in ['cancel','success']:
    with tempfile.TemporaryDirectory(prefix='panels-first-') as temp:
        root=Path(temp)/'root';root.mkdir();(root/'file').write_text('data')
        gate=Path(temp)/'gate';marker=Path(str(gate)+'.entered')
        os.environ['TFILE_SCAN_GATE']=str(gate)
        t=display_pty.Terminal(str(root))
        try:
            t.send(F9+HOME+DOWN*17+'\n');t.send(' ')
            gate.write_text(str(root))
            os.write(t.master,(F9+HOME+DOWN*18+'\n').encode())
            until(t,lambda:marker.exists() and marker.read_text().strip())
            pid=int(marker.read_text());start=time.monotonic()
            if action=='cancel':
                os.write(t.master,b'\x1b')
                if baseline:
                    end=time.monotonic()+.8
                    while time.monotonic()<end:pump(t)
                    assert 'Reading directory' not in text(t)
                    gate.unlink()
                    until(t,lambda:'Left*' in text(t))
                else:
                    until(t,lambda:'Directory load cancelled' in text(t),.6)
                    assert 'Left*' not in text(t) and 'Marked: 1' in text(t),text(t)
                    assert gate.exists()
                    gate.unlink()
                    t.send(F9+HOME+DOWN*18+'\n');until(t,lambda:'Left*' in text(t))
            else:
                gate.unlink();until(t,lambda:'Left*' in text(t))
                assert 'Marked: 1' in text(t)
            print(f'{"before" if baseline else "after"} first dual/{action}: {(time.monotonic()-start)*1000:.1f}ms worker_is_ui={pid==t.proc.pid}',flush=True)
        finally:
            gate.unlink(missing_ok=True);t.close();os.environ.pop('TFILE_SCAN_GATE',None)
