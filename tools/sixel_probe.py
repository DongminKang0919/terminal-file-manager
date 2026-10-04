#!/usr/bin/env python3
"""Run in the user's real terminal: bounded capability query and visual checks.
This standalone diagnostic does not run inside tfile or consume tfile's keys.
"""
import argparse
import curses
import os
import re
import select
import shutil
import subprocess
import sys
import tempfile
import termios
import time
from pathlib import Path


def query():
    fd=sys.stdin.fileno();old=termios.tcgetattr(fd);new=termios.tcgetattr(fd)
    new[3]&=~(termios.ICANON|termios.ECHO);new[6][termios.VMIN]=0;new[6][termios.VTIME]=0
    data=b''
    try:
        termios.tcsetattr(fd,termios.TCSANOW,new)
        os.write(sys.stdout.fileno(),b'\x1b[c\x1b[16t')
        end=time.monotonic()+.30
        while time.monotonic()<end and len(data)<512:
            if select.select([fd],[],[],max(0,end-time.monotonic()))[0]:data+=os.read(fd,512-len(data))
    finally:termios.tcsetattr(fd,termios.TCSANOW,old)
    da=re.search(rb'\x1b\[\?([0-9;]+)c',data)
    cells=re.search(rb'\x1b\[6;(\d+);(\d+)t',data)
    supported=bool(da and 4 in [int(x) for x in da[1].split(b';')][1:])
    pixels=(int(cells[2]),int(cells[1])) if cells else None
    rest=re.sub(rb'\x1b\[(?:\?[0-9;]+c|6;\d+;\d+t)',b'',data)
    print('DA:',repr(da[0] if da else b'no response'))
    print('Cell pixels:',pixels or 'no response')
    if rest:print('Unmatched input (never interpreted as commands):',repr(rest))
    return supported,pixels


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cell-pixels',help='Measured WIDTHxHEIGHT if CSI 16 t is unsupported')
    args=parser.parse_args()
    if not sys.stdin.isatty() or not sys.stdout.isatty():
        print('No real interactive terminal: pixel display/erase/overlay/resize cannot be verified.');return 2
    print('Querying for 300 ms. This is a separate diagnostic, before starting tfile.');sys.stdout.flush()
    supported,cells=query()
    if args.cell_pixels:
        m=re.fullmatch(r'(\d+)x(\d+)',args.cell_pixels)
        if not m:parser.error('--cell-pixels requires WIDTHxHEIGHT')
        cells=tuple(map(int,m.groups()))
    if not cells or not (0<cells[0]<=64 and 0<cells[1]<=128):
        print('Need measured cell dimensions; rerun with --cell-pixels WIDTHxHEIGHT.');return 2
    print('DA advertises Sixel:',supported,'(a response alone does not prove rendering/erase).')
    image=shutil.which('magick') or shutil.which('convert')
    if not image:print('ImageMagick missing. Install imagemagick to run the visual experiment.');return 2
    with tempfile.TemporaryDirectory(prefix='tfile-sixel-probe-') as directory:
        sample=Path(directory)/'sample.png'
        subprocess.run([image,'-size','120x60','gradient:red-blue',str(sample)],check=True,timeout=8)
        sixel=subprocess.run([image,str(sample),'-thumbnail','120x60>','-colors','64','sixel:-'],capture_output=True,check=True,timeout=8).stdout
        print('Standalone: a red/blue rectangle should appear below.');sys.stdout.flush()
        sys.stdout.buffer.write(b'\x1b7\x1b[?80s\x1b[?80l'+sixel+b'\x1b[?80r\x1b8'+b'\n'*((60+cells[1]-1)//cells[1]+1));sys.stdout.buffer.flush()
        input('Press Enter after checking the image. ')
        sys.stdout.write('\x1b[2J\x1b[H');sys.stdout.flush()
        standalone=input('Did the image appear and clear completely? [y/N] ').lower()=='y'
        input('Next: ncurses panel. m toggles a modal, resize the window, q ends. Press Enter. ')
        def experiment(screen):
            curses.curs_set(0);modal=False
            while True:
                # Same ordering as tfile: erase pixels, force cell repaint, refresh, image.
                sys.stdout.write('\x1b[0m\x1b[2J\x1b[H');sys.stdout.flush();screen.clearok(True);screen.erase()
                h,w=screen.getmaxyx();split=w//2
                if h>=12 and w>=50:
                    screen.addstr(0,1,'Sixel + ncurses: m modal, resize, q quit')
                    screen.addstr(2,2,'Left panel must stay clean')
                    win=curses.newwin(h-4,w-split,2,split);win.box();win.addstr(1,2,'Image panel');
                    screen.refresh();win.refresh()
                    if not modal:
                        px=min(120,(w-split-4)*cells[0]);py=min(60,(h-9)*cells[1])
                        image_bytes=subprocess.run([image,str(sample),'-thumbnail',f'{px}x{py}>','-colors','64','sixel:-'],capture_output=True,check=True,timeout=8).stdout
                        sys.stdout.write(f'\x1b7\x1b[?80s\x1b[?80l\x1b[6;{split+3}H');sys.stdout.flush()
                        sys.stdout.buffer.write(image_bytes+b'\x1b[?80r\x1b8');sys.stdout.buffer.flush()
                    else:
                        dialog=curses.newwin(7,min(46,w-4),(h-7)//2,(w-min(46,w-4))//2)
                        dialog.box();dialog.addstr(2,2,'No image may remain over this modal');dialog.addstr(4,2,'m: close modal, q: finish');dialog.refresh()
                else:screen.addstr(0,0,'Resize to at least 50x12');screen.refresh()
                key=screen.getch()
                if key==ord('q'):break
                if key==ord('m'):modal=not modal
            sys.stdout.write('\x1b[2J\x1b[H');sys.stdout.flush()
        curses.wrapper(experiment)
        panels=input('Were panel bounds, clearing, modal overlap and resizing correct? [y/N] ').lower()=='y'
        print('Visual confirmation:',{'standalone':standalone,'ncurses_panel_modal_resize':panels})
        if standalone and panels:
            print(f'Run: TFILE_SIXEL=1 TFILE_CELL_PIXELS={cells[0]}x{cells[1]} ./tfile')
            print('Also check PNG/JPEG/PDF selection, F1/F7 popups and Auto/Off inside tfile.')
        else:print('Keep image preview disabled/unconfirmed in this terminal.')
    return 0

if __name__=='__main__':raise SystemExit(main())
