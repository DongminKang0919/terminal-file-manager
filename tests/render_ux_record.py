"""Render captured terminal cells in monochrome; requires optional ImageMagick.
This does not reconstruct terminal colors, attributes, pixels or timing.
Usage: python3 tests/render_ux_record.py screens.json WIDTH MODE STATE out.png
"""
import json
from pathlib import Path
import subprocess
import sys

source,width,mode,state,out=sys.argv[1:]
record=next(r for r in json.loads(Path(source).read_text()) if r['size'][0]==int(width) and r['mode']==int(mode) and r['state']==state)
font='/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf'
cjk='/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf'
commands=[]
for y,row in enumerate(record['rows']):
 # rows contain wide glyphs with their continuation cells omitted in the JSON
 # string. Count terminal cell width again for placement.
 import unicodedata
 x=0
 for char in row:
  cells=2 if unicodedata.east_asian_width(char) in ('W','F') else 1
  if char!=' ':
   escaped=char.replace('\\','\\\\').replace("'","\\'")
   commands.append(f"font '{cjk if cells==2 else font}' text {12+x*10},{22+y*20} '{escaped}'")
  x+=cells
subprocess.run(['convert','-size',f'{record["size"][0]*10+24}x{record["size"][1]*20+16}','xc:#16181c','-fill','#e6e6e6','-pointsize','16','-draw',' '.join(commands),out],check=True)
print(out)
