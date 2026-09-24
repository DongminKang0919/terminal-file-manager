"""Guard the dependency direction and keep native APIs inside platform backends."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1] / 'src'
errors = []
for path in root.rglob('*'):
    if path.suffix not in ('.c', '.h'):
        continue
    text = path.read_text()
    rel = path.relative_to(root)
    layer = rel.parts[0]
    if layer == 'platform':
        if re.search(r'#include\s+["<].*(?:ui/|core/|ncurses)', text):
            errors.append(f'{rel}: platform depends on an upper layer')
        continue
    if layer == 'core' or rel.name.startswith('model.'):
        if re.search(r'#include\s+["<].*(?:ncurses|ui/|dirent|unistd|sys/|fcntl|windows\.h)', text):
            errors.append(f'{rel}: native/UI header outside platform')
        if re.search(r'\b(?:WINDOW|MEVENT|PATH_MAX|mode_t|ssize_t)\b|struct\s+stat\b', text):
            errors.append(f'{rel}: native/UI type in core')
    if layer == 'ui' and re.search(r'#include\s+["<].*(?:platform/|dirent|unistd|sys/|fcntl|windows\.h)', text):
        errors.append(f'{rel}: UI bypasses core')
    # Remove strings/comments before scanning calls (labels may contain "open").
    code = re.sub(r'"(?:[^"\\]|\\.)*"|/\*[\s\S]*?\*/|//[^\n]*', '', text)
    banned = r'\b(?:opendir|readdir|closedir|stat|lstat|fstat|mkdir|open|rename|renameat2|unlink|rmdir|readlink|symlink|realpath|getcwd|chdir|fopen|fread|fgets|read|write|clock_gettime|system|popen|strcasecmp|strdup)\s*\('
    if re.search(banned, code):
        errors.append(f'{rel}: filesystem/OS call outside platform')
    if layer == 'core' and re.search(r"(?:strrchr|strchr)\s*\([^\n]*['\"]/|\bPATH_MAX\b", code):
        errors.append(f'{rel}: native path rule in core')
assert not errors, '\n'.join(errors)
print('PASS: UI -> core -> platform dependency boundaries')
