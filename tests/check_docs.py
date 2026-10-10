"""Check local links and GitHub-style heading anchors in entry-point documents."""
from pathlib import Path
import re
import unicodedata
from urllib.parse import unquote

root = Path(__file__).resolve().parents[1]
files = [root / 'README.md', *(root / 'docs' / name for name in
         ['USER_GUIDE.md', 'REFERENCE.md', 'RELEASE_READINESS_2026-10-10.md'])]

def anchors(path):
    result = set()
    counts = {}
    fenced = False
    for line in path.read_text().splitlines():
        if line.startswith('```'):
            fenced = not fenced
        if fenced or not re.match(r'^#{1,6} ', line):
            continue
        title = re.sub(r'^#+\s+', '', line).strip().lower()
        title = re.sub(r'<[^>]*>', '', title)
        slug = ''.join(c for c in title if c in ' -_' or
                       unicodedata.category(c)[0] in ('L', 'N', 'M')).replace(' ', '-')
        count = counts.get(slug, 0)
        counts[slug] = count + 1
        result.add(slug + (f'-{count}' if count else ''))
    return result

failures = []
for path in files:
    for destination in re.findall(r'!?\[[^\]]*\]\(([^)]+)\)', path.read_text()):
        if re.match(r'^[a-z]+://', destination):
            continue  # Network references are reviewed separately.
        filename, _, fragment = unquote(destination).partition('#')
        target = (path.parent / filename).resolve() if filename else path
        if not target.exists():
            failures.append(f'{path.relative_to(root)}: missing {destination}')
        elif fragment and target.suffix == '.md' and fragment not in anchors(target):
            failures.append(f'{path.relative_to(root)}: missing anchor {destination}')
if failures:
    raise SystemExit('\n'.join(failures))
print('PASS: entry-point local document links and heading anchors')
