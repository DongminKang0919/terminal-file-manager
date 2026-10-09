"""Visible fixed marks/menus/policies and real single-mark operations on fixtures."""
import os,tempfile
from pathlib import Path
from display_pty import Terminal
F5='\x1b[15~';F6='\x1b[17~';F8='\x1b[19~';F9='\x1b[20~';HOME='\x1bOH';END='\x1bOF';DOWN='\x1bOB'
def body(t):return '\n'.join(t.screen.row(i) for i in range(t.screen.height))
def marks(t,n):assert f'Marked: {n}' in body(t),body(t)
def dest(t,path):t.send(HOME+'\x1b[3~'*200+str(path)+'\n\n\n')
for w,h in [(50,9),(100,24)]:
 for action in ['copy','move','delete','trash','rename']:
  with tempfile.TemporaryDirectory(prefix='tfile-single-mark-') as d:
   base=Path(d);src=base/'src';dst=base/'dst';src.mkdir();dst.mkdir();data=base/'data'
   os.environ.update(XDG_CONFIG_HOME=str(base/'config'),XDG_DATA_HOME=str(data))
   cursor=src/'a cursor';target=src/'z marked';cursor.write_text('cursor');target.write_text('target')
   t=Terminal(str(src),w,h)
   try:
    assert 'Space: Mark' in t.screen.row(h-1) and 'e: Vim' in t.screen.row(h-1)
    t.send(END+' '+HOME);marks(t,1)
    assert 'Targets: 1 marked' in body(t),body(t)
    assert any('> a cursor' in t.screen.row(i) for i in range(h))
    assert any(' *z marked' in t.screen.row(i) for i in range(h))
    if action in ['copy','move']:
     t.send(F5 if action=='copy' else F6);dest(t,dst)
     assert (dst/'z marked').read_text()=='target' and not (dst/'a cursor').exists()
     assert target.exists()==(action=='copy')
    elif action=='rename':
     t.send(F9+DOWN*11+'\n'+HOME+'\x1b[3~'*100+'renamed mark\n')
     assert (src/'renamed mark').read_text()=='target' and not target.exists()
    else:
     command=F8 if action=='delete' else 't'
     t.send(command);assert '(1 target)' in body(t)
     t.send('\n');assert target.exists();marks(t,1)
     t.send(command+'\t\n');assert not target.exists()
     if action=='trash':
      payloads=list((data/'Trash/files').iterdir());assert len(payloads)==1 and payloads[0].read_text()=='target'
    assert cursor.read_text()=='cursor';marks(t,0)
   finally:t.close()
 with tempfile.TemporaryDirectory(prefix='tfile-mark-menu-') as d:
  base=Path(d);src=base/'src';src.mkdir();os.environ.update(XDG_CONFIG_HOME=str(base/'config'),XDG_DATA_HOME=str(base/'data'))
  for name in ['a visible','b filtered','c visible']:(src/name).write_text(name)
  t=Terminal(str(src),w,h)
  try:
   t.send(F9+DOWN*15);assert 'Select all visible' in body(t) and 'Clear all marks' in body(t)
   t.send('a');marks(t,3);t.send(F9+'u');marks(t,0)
   t.send('a');marks(t,3);t.send('u');marks(t,0)
   # Filtering clears marks and limits select-all and delete to the visible list.
   t.send('a'+'f\nvisible\n');marks(t,0)
   assert 'Space: Mark' in t.screen.row(h-1) and 'f: Filter/Clear' in t.screen.row(h-1)
   t.send(F9+'a');marks(t,2);t.send(F8)
   assert 'Batch delete confirmation' in body(t) and 'Targets: 2' in body(t) and 'Permanent deletion' in body(t)
   t.send('\x1bOF');assert 'c visible' in body(t),body(t)
   t.send('\n');assert len(list(src.iterdir()))==3;marks(t,2)
   t.send(F8+'\t\n');assert sorted(p.name for p in src.iterdir())==['b filtered'];marks(t,0)
   # Clear filter; no marks means the actual cursor, with no prior hidden targets.
   t.send('f'+DOWN*2+'\n');t.send('t');assert 'Confirm Trash (1 target)' in body(t);t.send('\n');assert (src/'b filtered').exists()
  finally:t.close()
 # Independent marks and dynamic active-panel policy, including narrow dual view.
 with tempfile.TemporaryDirectory(prefix='tfile-mark-panels-') as d:
  base=Path(d);src=base/'src';src.mkdir();os.environ.update(XDG_CONFIG_HOME=str(base/'config'),XDG_DATA_HOME=str(base/'data'))
  for name in ['a','b']:(src/name).write_text(name)
  t=Terminal(str(src),w,h)
  try:
   t.send(F9+DOWN*18+'\n'+'a');marks(t,2)
   t.send('\t');marks(t,0);assert 'Target: cursor' in body(t),body(t)
   t.send(' ');marks(t,1);t.send('\t');marks(t,2);assert 'Targets: 2 marked' in body(t)
   t.send('u');marks(t,0);t.send('\t');marks(t,1)
   t.send('a'+'t');assert 'Batch Trash confirmation' in body(t) and 'Targets: 2' in body(t)
   t.send('\x1bOF');assert '/b' in body(t);t.send('\n');assert len(list(src.iterdir()))==2;marks(t,2)
  finally:t.close()
print('PASS: fixed cursor/mark symbols, footer/count/target cues, menu/direct all-clear, exactly-one copy/move/delete/Trash/rename, filtered batch Cancel/list/permanent label and independent dual marks at 50x9/100x24')
