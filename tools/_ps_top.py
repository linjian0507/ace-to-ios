import re, sys
from pathlib import Path
p = Path(sys.argv[1])
lines = p.read_text(errors='replace').splitlines()
for start in [1859, 2854, 7810, 9829]:
    print(f'=== ps at line {start} ===')
    rows = []
    for j in range(start, min(start+130, len(lines))):
        m = re.match(r'\s*(\d+)\s+\?\?\s+(\d+):(\d+\.\d+)\s+(.*)', lines[j])
        if m:
            secs = int(m.group(2))*60 + float(m.group(3))
            rows.append((secs, m.group(4)[:90]))
    rows.sort(reverse=True)
    for secs, cmd in rows[:8]:
        print(f'  {secs:6.1f}s {cmd}')
