"""Inspect actual PTY screens and byte-exact operations on hostile file names.
The small VT reader implements the cursor/edit/ACS sequences used by ncurses;
it is a test helper, not a Unicode shaping engine or application component.
"""
import codecs
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import struct
import subprocess
import tempfile
import termios
import time
import unicodedata

BINARY = os.path.abspath(os.environ.get('TFILE_BINARY', './tfile'))

class Screen:
    def __init__(self, height, width):
        self.height, self.width = height, width
        self.rows = [[' '] * width for _ in range(height)]
        self.y = self.x = 0
        self.acs = False
        self.pending = ''
        self.last = ' '
        self.decoder = codecs.getincrementaldecoder('utf-8')('strict')

    def put(self, char):
        if self.acs:
            char = {'l': '┌', 'k': '┐', 'm': '└', 'j': '┘', 'q': '─', 'x': '│'}.get(char, char)
        cells = 2 if unicodedata.east_asian_width(char) in ('W', 'F') else 1
        if self.x >= self.width:
            self.x = 0
            self.y = min(self.height - 1, self.y + 1)
        assert self.x + cells <= self.width, 'wide character crosses screen edge'
        self.rows[self.y][self.x] = char
        if cells == 2:
            self.rows[self.y][self.x + 1] = ''
        self.x += cells
        self.last = char

    def feed(self, data):
        text = self.pending + self.decoder.decode(data)
        at = 0
        while at < len(text):
            ch = text[at]
            if ch == '\x1b':
                if at + 1 == len(text): break
                kind = text[at + 1]
                if kind == '[':
                    end = at + 2
                    while end < len(text) and not ('@' <= text[end] <= '~'): end += 1
                    if end == len(text): break
                    self.csi(text[at + 2:end], text[end])
                    at = end + 1
                    continue
                if kind in '()':
                    if at + 2 == len(text): break
                    if kind == '(': self.acs = text[at + 2] == '0'
                    at += 3
                    continue
                at += 2  # application keypad modes and other nonprinting escapes
                continue
            if ch == '\r': self.x = 0
            elif ch == '\n': self.y = min(self.height - 1, self.y + 1)
            elif ch == '\b': self.x = max(0, self.x - 1)
            elif ch == '\t': self.x = min(self.width - 1, (self.x // 8 + 1) * 8)
            elif ord(ch) >= 32: self.put(ch)
            at += 1
        self.pending = text[at:]

    def csi(self, params, command):
        if params.startswith('?'): return
        args = [int(n) if n else 0 for n in params.split(';')] if params else [0]
        n = args[0] or 1
        if command in 'Hf':
            self.y = max(0, min(self.height - 1, n - 1))
            self.x = max(0, min(self.width - 1, (args[1] if len(args) > 1 and args[1] else 1) - 1))
        elif command == 'G': self.x = min(self.width - 1, n - 1)
        elif command == 'd': self.y = min(self.height - 1, n - 1)
        elif command == 'A': self.y = max(0, self.y - n)
        elif command == 'B': self.y = min(self.height - 1, self.y + n)
        elif command == 'C': self.x = min(self.width - 1, self.x + n)
        elif command == 'D': self.x = max(0, self.x - n)
        elif command == 'J':
            if args[0] == 2: self.rows = [[' '] * self.width for _ in range(self.height)]
            elif args[0] == 0:
                self.rows[self.y][self.x:] = [' '] * (self.width - self.x)
                for y in range(self.y + 1, self.height): self.rows[y] = [' '] * self.width
        elif command == 'K':
            start, end = (0, self.width) if args[0] == 2 else (0, self.x + 1) if args[0] == 1 else (self.x, self.width)
            self.rows[self.y][start:end] = [' '] * (end - start)
        elif command == 'X':
            end = min(self.width, self.x + n)
            self.rows[self.y][self.x:end] = [' '] * (end - self.x)
        elif command == 'P': self.rows[self.y] = (self.rows[self.y][:self.x] + self.rows[self.y][self.x + n:] + [' '] * n)[:self.width]
        elif command == '@': self.rows[self.y] = (self.rows[self.y][:self.x] + [' '] * n + self.rows[self.y][self.x:])[:self.width]
        elif command == 'b':
            for _ in range(n): self.put(self.last)
        elif command in 'mrhlt': pass  # color/style, scrolling region, modes
        else: raise AssertionError(f'Unsupported screen sequence: {params}{command}')

    def row(self, y): return ''.join(self.rows[y])

class Terminal:
    def __init__(self, root, width=100, height=24):
        self.screen = Screen(height, width)
        self.master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', height, width, 0, 0))
        self.proc = subprocess.Popen([BINARY, root], stdin=slave, stdout=slave, stderr=slave,
            env={**os.environ, 'TERM': 'xterm-256color', 'LC_ALL': 'C.UTF-8'})
        os.close(slave)
        try:
            self.read()
        except BaseException:
            self.proc.kill(); self.proc.wait(); os.close(self.master); raise

    def read(self):
        deadline = time.monotonic() + .12
        while time.monotonic() < deadline:
            if select.select([self.master], [], [], .01)[0]:
                try: data = os.read(self.master, 65536)
                except OSError: break
                assert b'\x1b[99;99H' not in data, 'filename interpreted as cursor command'
                self.screen.feed(data)

    def send(self, text):
        os.write(self.master, text.encode() if isinstance(text, str) else text)
        self.read()

    def click(self, x, y): self.send(f'\x1b[<0;{x+1};{y+1}M\x1b[<0;{x+1};{y+1}m')

    def frame(self, x, y, width, height):
        s = self.screen
        assert s.rows[y][x] == '┌' and s.rows[y][x+width-1] == '┐', s.row(y)
        assert s.rows[y+height-1][x] == '└' and s.rows[y+height-1][x+width-1] == '┘', s.row(y+height-1)
        for row in range(y+1, y+height-1):
            assert s.rows[row][x] == '│' and s.rows[row][x+width-1] == '│', s.row(row)

    def close(self):
        try:
            self.send('\x1b')
            self.send('q')
            assert self.proc.wait(timeout=2) == 0
        finally:
            if self.proc.poll() is None: self.proc.kill(); self.proc.wait()
            os.close(self.master)

def shown(raw):
    # Test oracle independent of the C implementation, including raw-byte escapes.
    result = ''
    for ch in raw.decode('utf-8', 'surrogateescape'):
        n = ord(ch)
        if 0xDC80 <= n <= 0xDCFF: result += f'\\x{n-0xDC00:02X}'
        elif ch == '\\': result += '\\\\'
        elif ch in '\n\r\t': result += {'\n': '\\n', '\r': '\\r', '\t': '\\t'}[ch]
        elif n < 32 or n == 127: result += f'\\x{n:02X}'
        elif unicodedata.category(ch) in ('Mn', 'Me', 'Cf', 'Cc', 'Zl', 'Zp'): result += f'\\u{{{n:04X}}}'
        else: result += ch
    return result

def select_name(t, root, name):
    names = sorted(os.listdir(root), key=bytes.lower)
    t.send('\x1bOH' + '\x1bOB' * names.index(name))

def prefix_transfer(t, move, prefix):
    t.send('\x1b[17~' if move else '\x1b[15~')
    t.send('\t\t\n')
    t.send('\x1bOH' + prefix + '\n')
    t.send('\t\t\n')

names = [b'n0\nline', b'n1\tname', b'n2\x01\x1b[99;99H', b'n3\xff\xc0',
         'n4-한글이름'.encode(), 'n5-e\u0301'.encode(), '\u0301n6'.encode()]
with tempfile.TemporaryDirectory(prefix='tfile-display-') as directory:
    root = os.fsencode(directory) + b'/path\n\t\xff'
    os.mkdir(root)
    twins = [shown(name).encode() for name in names]
    for i, name in enumerate(names):
        with open(root + b'/' + name, 'wb') as f: f.write(f'original-{i}'.encode())
        if twins[i] != name:
            with open(root + b'/' + twins[i], 'wb') as f: f.write(b'ordinary twin')
    t = Terminal(root)
    try:
        assert r'path\n\t\xFF' in t.screen.row(1), t.screen.row(1)
        for i, name in enumerate(names):
            select_name(t, root, name)
            # Main list and preview metadata use the same escaping policy.
            assert shown(name) in '\n'.join(t.screen.row(y) for y in range(4, 21))
            t.send('\x1b[19~')
            t.frame(11, 8, 78, 8)
            assert shown(name) in t.screen.row(10), t.screen.row(10)
            assert '[ Delete ]' in t.screen.row(14) and '[ Cancel ]' in t.screen.row(14)
            t.send('\n')  # Cancel remains the default.
            assert os.path.exists(root + b'/' + name)
            # Copy preserves raw bytes in the prefilled name, including invalid UTF-8.
            prefix_transfer(t, False, 'copy-')
            assert open(root + b'/copy-' + name, 'rb').read() == f'original-{i}'.encode()
            assert os.path.exists(root + b'/' + name)
            select_name(t, root, name)
            prefix_transfer(t, True, 'moved-')
            moved = b'moved-' + name
            assert not os.path.exists(root + b'/' + name)
            assert open(root + b'/' + moved, 'rb').read() == f'original-{i}'.encode()
            t.send('\x1b[19~')
            assert shown(moved) in t.screen.row(10)
            t.send('\t\n')
            assert not os.path.exists(root + b'/' + moved)
            if twins[i] != name: assert open(root + b'/' + twins[i], 'rb').read() == b'ordinary twin'
        # Inspect selector and search rows; no raw control bytes may move the cursor.
        t.send('\x1b[15~\n')  # Copy -> Choose source picker
        t.frame(7, 1, 86, 22)
        assert 'copy-n0\\nline' in '\n'.join(t.screen.row(y) for y in range(5, 18))
        select_name(t, root, b'copy-n0\nline')
        t.send(' ')  # Choose the raw newline item, not its literal-escape twin.
        t.send('\t\t\n\x1bOHpicked-\n\t\t\n')
        assert open(root + b'/picked-copy-n0\nline', 'rb').read() == b'original-0'
        t.send('\x1bORcopy-n0\n')
        t.frame(5, 1, 90, 22)
        assert 'copy-n0\\nline' in t.screen.row(4)
        t.send('\n\x1b[19~')
        assert 'copy-n0\\nline' in t.screen.row(10) and 'picked-' not in t.screen.row(10)
        t.send('\n')
    finally:
        t.close()

for width, height in [(50, 9), (100, 24)]:
    with tempfile.TemporaryDirectory(prefix='tfile-display-small-') as directory:
        root = os.fsencode(directory)
        first = ('long-' + '한' * 65 + '-first\tend').encode()
        second = ('long-' + '한' * 65 + '-second\tend').encode()
        for name in (first, second):
            with open(root + b'/' + name, 'wb') as f: f.write(name)
        t = Terminal(root, width, height)
        try:
            select_name(t, root, second)
            if width == 50:
                t.send('\x1bORlong\n')
                t.frame(4, 1, 42, 7)
                assert '...' in t.screen.row(4) and '[ Open ]' in t.screen.row(6)
                t.send('\x1b')
                t.send('\x1b[15~\n')
                t.frame(2, 1, 46, 7)
                assert '...' in t.screen.row(4) and '[ Cancel ]' in t.screen.row(6)
                t.send('\x1b\x1b')
            t.send('\x1b[19~')
            dw, dh = min(78, width-4), min(8, height-2)
            x, y = (width-dw)//2, (height-dh)//2
            collected = []
            for _ in range(8):
                t.frame(x, y, dw, dh)
                row = ''.join(t.screen.rows[y+2][x+2:x+dw-2]).rstrip()
                collected.append(row)
                assert '[ Delete ]' in t.screen.row(y+dh-2) and '[ Cancel ]' in t.screen.row(y+dh-2)
                match = re.search(r'Name (\d+)/(\d+)', t.screen.row(y+dh-3))
                assert match, t.screen.row(y+dh-3)
                if match[1] == match[2]: break
                t.click(x+dw-4, y+dh-3)
            assert ''.join(collected) == shown(second), collected
            # Previous page is available by keyboard, next page by mouse.
            t.send('\x1b[5~')
            assert ''.join(t.screen.rows[y+2][x+2:x+dw-2]).rstrip() == collected[-2]
            t.send('\n')
            assert os.path.exists(root+b'/'+second)
            t.send('\x1b[19~\t\n')
            assert not os.path.exists(root+b'/'+second) and os.path.exists(root+b'/'+first)
        finally:
            t.close()
print('PASS: PTY popup guards, escaped names, full delete-name paging and byte-exact copy/rename/delete')
