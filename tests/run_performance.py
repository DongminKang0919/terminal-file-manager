"""Reproducible local measurements; fixtures and binaries stay under /tmp.
No timing thresholds, cache eviction, user files, or sanitizer timings.
Example: python3 tests/run_performance.py --data /tmp/tfile-perf-DATA --output /tmp/before.json
Use the same --data for before/after and --source for an archived source tree.
"""
import argparse
import csv
import glob
import io
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import tempfile
import time

p = argparse.ArgumentParser()
p.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1])
p.add_argument('--data', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--reps', type=int, default=7)
a = p.parse_args()
assert a.reps >= 3
assert str(a.data.resolve()).startswith('/tmp/tfile-perf-'), 'Use a dedicated /tmp/tfile-perf-* fixture'
start = time.monotonic()
if not a.data.exists():
    a.data.mkdir()
    for label, n in [('small',10), ('list1000',1000), ('list10000',10000)]:
        directory = a.data/label; directory.mkdir()
        for i in range(n):
            file = directory/f'file-{i:05}.txt'; file.write_bytes(b'x'*(1+i%64))
            os.utime(file,(1700000000+i%100,1700000000+i%100))
    tree=a.data/'tree'; tree.mkdir()
    for i in range(200):
        d=tree/f'dir-{i:03}'; d.mkdir()
        for j in range(20): (d/f'f-{j:03}').write_bytes(b'content\n')
    small=a.data/'small-files'; small.mkdir()
    for i in range(500): (small/f'f-{i:04}').write_bytes(b'x'*64)
    (a.data/'large.bin').write_bytes(b'\x00'*(16*1024*1024))
    (a.data/'text.txt').write_text(''.join(f'line-{i:06} '+'x'*100+'\n' for i in range(100000)))
    (a.data/'out').mkdir()
    (a.data/'.fixture').write_text('tfile performance fixture v1\n')
else:
    assert (a.data/'.fixture').read_text()=='tfile performance fixture v1\n'
fixture_seconds=time.monotonic()-start
flags=['-D_XOPEN_SOURCE=700','-O2','-Wall','-Wextra','-Wpedantic','-std=c11']
source=a.source.resolve()
harness=Path(__file__).with_name('performance.c').resolve()
wrappers='qsort,malloc,calloc,realloc,initscr,input_key,input_wide,wrefresh,platform_directory_open,platform_reader_open,platform_reader_line'
with tempfile.TemporaryDirectory(prefix='tfile-perf-build-') as build:
    obj=Path(build)/'main.o'; binary=Path(build)/'measure'
    start=time.monotonic()
    subprocess.run(['cc',*flags,'-Dmain=tfile_main','-c',str(source/'src/ui/main.c'),'-o',str(obj)],check=True)
    sources=[source/'src/model.c',*sorted((source/'src/core').glob('*.c')),*sorted((source/'src/platform').glob('posix*.c')),
             *sorted(x for x in (source/'src/ui').glob('*.c') if x.name!='main.c')]
    includes=[f'-I{source}/src/{part}' for part in ['ui','platform']]
    subprocess.run(['cc',*flags,*includes,str(harness),*map(str,sources),str(obj),'-lncursesw',
                    '-Wl,'+','.join('--wrap='+x for x in wrappers.split(',')),'-o',str(binary)],check=True)
    build_seconds=time.monotonic()-start
    start=time.monotonic()
    raw=subprocess.run([str(binary),str(a.data),str(a.reps)],check=True,text=True,capture_output=True)
    measurement_seconds=time.monotonic()-start
rows=list(csv.DictReader(io.StringIO(raw.stdout)))
groups={}
for row in rows:
    for key in row:
        if key!='name': row[key]=float(row[key]) if key=='ms' else int(row[key])
    groups.setdefault(row['name'],[]).append(row)
summary={}
for name, values in groups.items():
    samples=[x['ms'] for x in values[1:]] if len(values)>1 else [values[0]['ms']]
    summary[name]={'first_ms':values[0]['ms'],'median_ms':statistics.median(samples),
                   'min_ms':min(samples),'max_ms':max(samples),
                   'counters':{key:[min(x[key] for x in values),max(x[key] for x in values)]
                               for key in values[0] if key not in ['name','run','ms']}}
report={'environment':{'kernel':platform.platform(),'cpu':platform.processor(),
                      'compiler':subprocess.check_output(['cc','--version'],text=True).splitlines()[0],
                      'ncurses':raw.stderr.strip(),
                      'flags':flags,'locale':'C.UTF-8','terminal':'xterm-256color, newterm -> /dev/null',
                      'cache':'No cache eviction; fixture creation warms the OS cache; first means first measured run.'},
        'source':str(source),'fixture':str(a.data),'reps':a.reps,'fixture_seconds':fixture_seconds,
        'build_seconds':build_seconds,'measurement_seconds':measurement_seconds,'summary':summary,'raw':rows}
a.output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
print(f'Saved measurements to {a.output}; generation {fixture_seconds:.3f}s, build {build_seconds:.3f}s, measurement {measurement_seconds:.3f}s')
