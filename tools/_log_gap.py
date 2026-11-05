import re, sys
from pathlib import Path
from datetime import datetime, timedelta

p = Path(sys.argv[1])
text = p.read_text(errors='replace').splitlines()
buckets = {}
for i, line in enumerate(text):
    m = re.search(r'2026-11-05 (13:2\d:\d\d)', line)
    if m:
        buckets.setdefault(m.group(1)[:8], []).append((i, line))
start = datetime(2026, 11, 5, 13, 25, 20)
for k in range(0, 135, 5):
    t = (start + timedelta(seconds=k)).strftime('%H:%M:%S')
    key = t[:8]
    rows = buckets.get(key, [])
    if rows:
        first = rows[0][1].strip()[:120]
        last = rows[-1][1].strip()[:120]
        print(f'{t} n={len(rows):4d} first={first}')
        print(f'{"":8}      last={last}')
    else:
        print(f'{t} n=0')
