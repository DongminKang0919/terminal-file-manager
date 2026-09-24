"""Exercise actual curses input through a PTY, using disposable files only."""
import fcntl
import os
from pathlib import Path
import pty
import select
import signal
import struct
import subprocess
import tempfile
import termios
import time

BINARY = Path(os.environ.get('TFILE_BINARY', Path(__file__).resolve().parents[1] / 'tfile'))

class Terminal:
    def __init__(self, directory, width=100):
        self.master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 24, width, 0, 0))
        self.proc = subprocess.Popen([str(BINARY), directory], stdin=slave, stdout=slave,
                                     stderr=slave, env={**os.environ, 'TERM': 'xterm-256color', 'LC_ALL': 'C.UTF-8'})
        os.close(slave)
        self.initial = self.read()

    def read(self):
        data = b''
        deadline = time.monotonic() + .15
        while time.monotonic() < deadline:
            if select.select([self.master], [], [], .02)[0]:
                try:
                    data += os.read(self.master, 65536)
                except OSError:
                    break
        return data

    def send(self, text):
        os.write(self.master, text.encode() if isinstance(text, str) else text)
        return self.read()

    def click(self, x, y):
        return self.send(f'\x1b[<0;{x + 1};{y + 1}M\x1b[<0;{x + 1};{y + 1}m')

    def rename_transfer(self, name):
        self.send('\t\t\n')  # Name field in transfer form
        self.send('\x1bOH' + '\x1b[3~' * 100 + name + '\n')
        return self.send('\t\t\n')  # Copy/Move now

    def close(self):
        self.send('\x1b')
        self.send('q')
        try:
            assert self.proc.wait(timeout=2) == 0
        finally:
            if self.proc.poll() is None:
                self.proc.kill()
            os.close(self.master)

# Top actions remain clickable in full and compact layouts. Deletion always
# requires an explicit confirmation, including recursive directory removal.
for width, delete_x, menu_x, quit_x in [(100, 67, 79, 89), (50, 32, 37, 42)]:
    with tempfile.TemporaryDirectory(prefix='tfile-delete-') as directory:
        root = Path(directory)
        target = root / 'sample.txt'
        target.write_text('keep until confirmed')
        t = Terminal(directory, width)
        try:
            labels = [b'F1', b'F2', b'F3', b'F5', b'F6', b'F7', b'F8', b'F9', b'F10']
            positions = [t.initial.index(label) for label in labels]
            assert positions == sorted(positions)
            assert b'Confirm deletion' in t.click(delete_x, 0)
            assert target.exists()
            t.send('\n')  # Enter alone chooses Cancel.
            assert target.exists()
            t.send('\x1b[3~')  # Delete key uses the same confirmation.
            t.send('\x1b')
            assert target.exists()
            t.click(delete_x, 0)
            # Dialog width is clamped on a narrow terminal.
            dialog_width = min(78, width - 4)
            dialog_x = (width - dialog_width) // 2
            t.click(dialog_x + 16, 14)  # Cancel button.
            assert target.exists()
            t.click(delete_x, 0)
            t.click(dialog_x + dialog_width - 4, 8)  # Close button.
            assert target.exists()
            t.click(delete_x, 0)
            t.send('\t\n')  # Explicitly choose Delete.
            assert not target.exists()
            nested = root / 'nested'
            nested.mkdir()
            (nested / 'child.txt').write_text('child')
            t.send('r')
            assert b'all its contents?' in t.click(delete_x, 0)
            t.send('\n')
            assert (nested / 'child.txt').exists()
            t.click(delete_x, 0)
            t.send('\t\n')
            assert not nested.exists()
            assert b'Menu' in t.click(menu_x, 0)
            t.send('\x1b')
            t.click(quit_x, 0)
            assert t.proc.wait(timeout=2) == 0
        finally:
            t.close()

with tempfile.TemporaryDirectory(prefix='tfile-test-') as directory:
    root = Path(directory)
    (root / 'folder').mkdir()
    (root / '.hidden').write_text('hidden')
    t = Terminal(directory)
    try:
        # F2 creates an editable modal. Korean text and cursor insertion survive.
        t.send('\x1bOQ')
        t.send('한글.txt')
        t.send('\x1bOH')  # Home
        t.send('new-')
        t.click(16, 14)  # OK button in 76x9 centered modal
        assert (root / 'new-한글.txt').exists(), list(root.iterdir())
        # Closing a creation dialog does not create its draft.
        t.send('\x1bOQ')
        t.send('cancelled')
        t.click(83, 7)
        assert not (root / 'cancelled').exists()
        # Copy selected file, rename selected source, and cancel then confirm delete.
        t.send('\x1b[15~')
        t.rename_transfer('copy.txt')
        assert (root / 'copy.txt').exists()
        # Go to last item (the created file); move it.
        t.send('\x1bOF')
        t.send('\x1b[17~')
        t.rename_transfer('renamed.txt')
        assert (root / 'renamed.txt').exists()
        assert not (root / 'new-한글.txt').exists()
        t.send('\x1bOF')
        t.send('\x1b[19~')
        t.send('\n')  # Safe default: cancel
        assert (root / 'renamed.txt').exists()
        t.send('\x1b[19~')
        t.click(15, 14)
        assert not (root / 'renamed.txt').exists()
        # Menu and options can be opened/closed with only the mouse.
        assert b'Menu' in t.click(79, 0)
        assert b'Options' in t.click(30, 11)
        t.click(30, 9)  # hidden files option
        t.click(70, 8)  # options close
        # Help close, search input close, then result close.
        t.send('\x1bOP')
        t.click(84, 1)
        t.send('\x1bOR')
        t.click(90, 1)
        t.send('\x1bOR')
        t.send('copy\n')
        t.click(90, 1)
        # Double-click directory and click Up to return.
        t.click(5, 4)
        t.click(5, 4)
        t.send('\x1bOQ')
        t.send('inside\n')
        assert (root / 'folder' / 'inside').exists()
        t.click(3, 1)
        t.send('\x1bOQ')
        t.send('outside\n')
        assert (root / 'outside').exists()
        # Resize during an input cancels cleanly without creating a file.
        t.send('\x1bOQ')
        t.send('resize-cancelled')
        fcntl.ioctl(t.master, termios.TIOCSWINSZ, struct.pack('HHHH', 9, 50, 0, 0))
        t.proc.send_signal(signal.SIGWINCH)
        t.read()
        assert not (root / 'resize-cancelled').exists()
    finally:
        t.close()
with tempfile.TemporaryDirectory(prefix='tfile-scroll-') as directory:
    root = Path(directory)
    for i in range(30):
        (root / f'{i:02d}.txt').write_text(str(i))
    t = Terminal(directory)
    try:
        # Wheel over preview must not move the list; wheel over list scrolls 1 row.
        t.send('\x1b[<65;80;5M')
        t.send('\x1b[<65;5;5M')
        t.send('\x1b[15~')
        t.rename_transfer('copied.txt')
        assert (root / 'copied.txt').read_text() == '1'
    finally:
        t.close()

with tempfile.TemporaryDirectory(prefix='tfile-path-') as directory:
    root = Path(directory)
    (root / 'folder').mkdir()
    (root / 'alias').symlink_to(root / 'folder', target_is_directory=True)
    t = Terminal(directory)
    try:
        for destination in ('folder/../folder', 'alias'):
            t.send('\x1b[15~')
            t.send('\t\n')  # Destination picker
            t.send('p')
            t.send('\x1bOH' + '\x1b[3~' * 200 + str(root / destination) + '\n')
            t.send(' ')  # Use this folder
            output = t.send('\t\t\t\n')
            assert b'Cannot copy' in output
            assert not (root / 'folder' / 'folder').exists()
            t.send('\x1b')
        # Minimum-size help/options/input remain dismissible.
        fcntl.ioctl(t.master, termios.TIOCSWINSZ, struct.pack('HHHH', 9, 50, 0, 0))
        t.proc.send_signal(signal.SIGWINCH)
        t.read()
        t.send('\x1b[18~')  # F7 options
        t.send('\x1b')
        t.send('\x1bOQ')
        t.send('small\n')
        assert (root / 'small').exists()
        t.send('\x1b[15~')
        t.rename_transfer('small-copy')
        assert (root / 'small-copy').exists()
    finally:
        t.close()

with tempfile.TemporaryDirectory(prefix='tfile-new-') as directory:
    root = Path(directory)
    (root / 'existing').write_text('keep me')
    (root / 'dangling').symlink_to(root / 'missing')
    t = Terminal(directory)
    try:
        # One form switches type without losing the entered name.
        t.send('\x1bOQ')
        t.send('existing')
        assert b'already exists' in t.send('\n')
        assert (root / 'existing').read_text() == 'keep me'
        t.send('-folder')
        t.click(33, 8)  # Folder radio
        t.click(16, 14)  # Create
        assert (root / 'existing-folder').is_dir()
        t.send('\x1bOQ')
        t.send('dangling')
        assert b'already exists' in t.send('\n')
        assert (root / 'dangling').is_symlink()
        t.send('-file\n')
        assert (root / 'dangling-file').is_file()
        # Keyboard type selector and validation preserve the draft as well.
        t.send('\x1bOQ')
        t.send('keyboard/')
        assert b'single name' in t.send('\n')
        t.send('\x7f')
        t.send('\x1b[Z')  # Shift-Tab from name to type
        t.send('\x1bOC')  # Right selects folder
        t.send('\n')  # Back to name
        t.send('\n')
        assert (root / 'keyboard').is_dir()
        t.send('\x1bOQ')
        assert b'Enter a name' in t.send('\n')
        t.send('\x1b')
    finally:
        t.close()

with tempfile.TemporaryDirectory(prefix='tfile-transfer-') as directory:
    root = Path(directory)
    (root / 'target').mkdir()
    (root / 'a-source.txt').write_text('original content')
    (root / 'b-other.txt').write_text('other content')
    t = Terminal(directory)
    try:
        # Entire copy workflow uses mouse selection, retaining the original name.
        t.send('\x1b[15~')
        t.click(15, 6)  # Choose source
        t.click(20, 5)  # a-source after target folder
        t.click(12, 21)  # Use selected
        t.click(15, 7)  # Browse destination
        t.click(20, 4)
        t.click(20, 4)  # Enter target folder
        t.click(12, 21)  # Use this folder
        t.click(15, 10)  # Copy now
        assert (root / 'target' / 'a-source.txt').read_text() == 'original content'
        assert (root / 'a-source.txt').exists()
        # Change source to another item, then move it with the original name.
        t.send('\x1b[17~')
        t.click(15, 6)
        t.click(20, 6)  # b-other
        t.click(12, 21)
        t.click(15, 7)
        t.click(20, 4)
        t.click(20, 4)
        t.click(12, 21)
        t.click(15, 10)
        assert not (root / 'b-other.txt').exists()
        assert (root / 'target' / 'b-other.txt').read_text() == 'other content'
        # Duplicate copy remains open and preserves existing contents.
        t.send('\x1bOF')  # a-source
        t.send('\x1b[15~')
        assert b'already exists' in t.click(15, 10)
        t.click(15, 8)  # Rename
        t.send('\x1bOH' + '\x1b[3~' * 100 + 'renamed-copy.txt\n')
        t.click(15, 10)
        assert (root / 'renamed-copy.txt').read_text() == 'original content'
        assert (root / 'a-source.txt').read_text() == 'original content'
        # Cancelling nested picker returns to form; closing form does nothing.
        t.send('\x1b[15~')
        t.click(15, 7)
        t.send('\x1b')
        t.send('\x1b')
        t.send('\x1b[15~')
        t.click(15, 7)
        fcntl.ioctl(t.master, termios.TIOCSWINSZ, struct.pack('HHHH', 12, 60, 0, 0))
        t.proc.send_signal(signal.SIGWINCH)
        t.read()
        # Both nested picker and parent form close on resize.
        t.send('\x1bOQ')
        t.send('after-resize\n')
        assert (root / 'after-resize').exists()
    finally:
        t.close()

with tempfile.TemporaryDirectory(prefix='tfile-preview-') as directory:
    root = Path(directory)
    (root / 'a-long.txt').write_text('START_OF_LONG_PREVIEW\n' + ''.join(f'content line {i:03d}\n' for i in range(100)) + 'END_OF_LONG_PREVIEW\n')
    (root / 'b-short.txt').write_text('SHORT_PREVIEW_CONTENT\n')
    (root / 'subdir').mkdir()
    t = Terminal(directory)
    try:
        # Panel toolbar opens the selected directory and returns to its parent.
        t.click(12, 3)
        t.send('\x1bOQ')
        t.send('inside\n')
        assert (root / 'subdir' / 'inside').exists()
        t.click(4, 3)
        assert b'OF_LONG_PREVIEW' in t.click(15, 5)
        output = b''
        output += t.send('\x1b[<65;80;10M' * 110)
        assert b'END_OF_LONG_PREVIEW' in output
        # End boundary does not advance into an empty page or affect list selection.
        t.send('\x1b[15~')
        t.rename_transfer('long-copy.txt')
        assert (root / 'long-copy.txt').read_text() == (root / 'a-long.txt').read_text()
        t.click(15, 5)  # reselect original long file
        for _ in range(8):
            t.send('\x1b[<65;80;10M')
        assert b'SHORT_PREVIEW_CONTENT' in t.click(15, 6)
        assert b'OF_LONG_PREVIEW' in t.click(15, 5)
        t.send('\x1b[<65;80;10M' * 20)
        output = t.send('\x1b[<64;80;10M' * 20)
        assert b'OF_LONG_PREVIEW' in output
        # On a short list the wheel still changes selection (one row at a time).
        t.send('\x1bOH')
        t.send('\x1b[<65;5;6M')
        t.send('\x1b[<65;5;6M')
        t.send('\x1b[<65;5;6M')
        t.send('\x1b[15~')
        t.rename_transfer('wheel-copy.txt')
        assert (root / 'wheel-copy.txt').read_text() == (root / 'long-copy.txt').read_text()
    finally:
        t.close()

with tempfile.TemporaryDirectory(prefix='tfile-history-') as directory:
    root = Path(directory)
    (root / 'a').mkdir()
    (root / 'b').mkdir()
    (root / 'match.txt').write_text('search me')
    t = Terminal(directory)
    try:
        # Search uses a single frame: the same visible X closes input and results.
        t.send('\x1bOR')
        t.click(90, 1)
        t.send('\x1bOQ')
        t.send('after-query-close\n')
        assert (root / 'after-query-close').exists()
        t.send('\x1bOR')
        t.send('match\n')
        t.click(90, 1)
        t.send('\x1bOQ')
        t.send('after-result-close\n')
        assert (root / 'after-result-close').exists()
        # Queue a close at scan start, before the synchronous engine finishes.
        t.send('\x1bOR')
        t.send('match\n\x1b[<0;91;2M\x1b[<0;91;2m')
        t.send('\x1bOQ')
        t.send('after-scan-close\n')
        assert (root / 'after-scan-close').exists()
        # SGR buttons 8/9 are back/forward; releases must not trigger twice.
        t.click(15, 4)
        t.click(15, 4)
        t.send('\x1b[<128;20;8M\x1b[<128;20;8m')
        t.send('\x1bOQ')
        t.send('back-at-root\n')
        assert (root / 'back-at-root').exists()
        t.send('\x1b[<129;20;8M\x1b[<129;20;8m')
        t.send('\x1bOQ')
        t.send('forward-in-a\n')
        assert (root / 'a' / 'forward-in-a').exists()
        t.send('\x1b[1;3D')  # Alt+Left fallback
        t.click(15, 5)
        t.click(15, 5)  # New branch: b replaces the old forward history.
        t.send('\x1b[<129;20;8M\x1b[<129;20;8m')
        t.send('\x1bOQ')
        t.send('branch-in-b\n')
        assert (root / 'b' / 'branch-in-b').exists()
        t.click(2, 1)  # Back
        t.click(6, 1)  # Forward
        t.send('\x1bOQ')
        t.send('modal-')
        t.send('\x1b[<128;20;8M\x1b[<128;20;8m')
        t.send('safe\n')
        assert (root / 'b' / 'modal-safe').exists()
        assert not (root / 'modal-safe').exists()
        # Parent is distinct from Back: it always goes to the containing directory.
        t.click(4, 3)
        t.send('\x1bOQ')
        t.send('parent-at-root\n')
        assert (root / 'parent-at-root').exists()
    finally:
        t.close()

print('PASS: single-window search close, one-row wheel, SGR side buttons/history, modals, file operations, preview')
