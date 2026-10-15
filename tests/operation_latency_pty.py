"""Measure real local fixtures separately from synthetic syscall stalls."""
import os
from pathlib import Path
import tempfile
import time
import display_pty
from preview_latency_pty import pump, until, text

display_pty.BINARY=os.path.abspath('tests/tfile_operation_latency')
F5='\x1b[15~';F8='\x1b[19~';F9='\x1b[20~';HOME='\x1bOH';DOWN='\x1bOB'
for kind in ['large-copy','tree-copy','tree-delete']:
    phases=['real','cancel','read','write','metadata'] if kind=='large-copy' else ['real','cancel','metadata','readdir','unlink'] if kind=='tree-delete' else ['real','cancel','read','write','metadata','readdir']
    for phase in phases:
        with tempfile.TemporaryDirectory(prefix='operation-latency-') as temp:
            root=Path(temp)/'root';root.mkdir();source=root/'source';target=root/'copy-source'
            if kind=='large-copy':
                with source.open('wb') as f:
                    for _ in range(64):f.write(b'x'*1048576)
            else:
                source.mkdir()
                for i in range(2000):(source/f'{i:04}').write_bytes(b'x'*512)
            log=Path(temp)/'result';marker=Path(str(log)+'.entered')
            os.environ['TFILE_OPERATION_LOG']=str(log);os.environ['TFILE_OPERATION_PHASE']=phase
            t=display_pty.Terminal(str(root))
            try:
                t.send(F9+HOME+DOWN*18+'\n') # Both panels show the same disposable directory.
                assert 'Left*' in text(t),text(t)
                if kind.endswith('copy'):
                    t.send(F5);t.send('\t'+HOME+'copy-\n');os.write(t.master,b'\n')
                else:os.write(t.master,(F8+'\t\n').encode())
                cancel=None
                if phase!='real':
                    until(t,lambda:marker.exists())
                    cancel=time.monotonic();os.write(t.master,b'\x1b')
                until(t,lambda:log.exists() and bool(log.read_text().strip()),10)
                elapsed,gap,code,items,copied,partial=log.read_text().split()
                latency=(time.monotonic()-cancel)*1000 if cancel else None
                if phase=='real':assert code=='0',log.read_text()
                else:
                    assert code=='11',log.read_text()
                    assert (latency<600 if phase=='cancel' else latency>750),latency
                    until(t,lambda:'cancel' in text(t).lower())
                t.read()
                # Cancel leaves the source and any already-created output in place.
                assert source.exists() or (phase=='real' and kind=='tree-delete')
                actual=target.stat().st_size if target.is_file() else len(list(target.iterdir())) if target.exists() else 0
                remaining=len(list(source.iterdir())) if source.is_dir() else source.stat().st_size if source.exists() else 0
                print(f'{kind}/{phase}: operation={elapsed}ms max_callback_gap={gap}ms cancel_input={f"{latency:.1f}ms" if latency else "not requested"} result={code} items={items} bytes={copied} partial={partial} target_size_or_children={actual} source_size_or_children={remaining}',flush=True)
                # Peer refresh is visible before returning to navigation.
                if kind.endswith('copy'):
                    t.send('\x1b');t.send('\t');assert 'Right*' in text(t),text(t)
                    if target.exists():assert 'copy-source' in text(t),text(t)
                else:
                    t.send('\t');assert 'Right*' in text(t),text(t)
                    for left,right in [(0,50),(50,100)]:
                        listing='\n'.join(t.screen.row(y)[left:right] for y in range(5,21))
                        assert ('source' in listing)==source.exists(),text(t)
            finally:
                t.close();os.environ.pop('TFILE_OPERATION_LOG',None);os.environ.pop('TFILE_OPERATION_PHASE',None)
