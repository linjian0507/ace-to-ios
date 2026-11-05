import re, sys
from pathlib import Path
p = Path(sys.argv[1])
lines = p.read_text(errors='replace').splitlines()
print('total lines', len(lines))
sb = []
surf = []
for i, line in enumerate(lines, 1):
    if 'SpringBoard' in line and re.search(r'13:2\d:\d\d', line):
        sb.append((i, line))
    if 'MiniIOSurface: SpringBoard' in line:
        surf.append((i, line))
print('--- SpringBoard lines in 13:26:00-13:27:00 ---')
for i, line in sb:
    m = re.search(r'13:(2\d):(\d\d)', line)
    if m and m.group(1) == '26':
        print(i, line[:170])
print('--- first 12 SpringBoard surfaces ---')
for i, line in surf[:12]:
    print(i, line[:170])
print('--- last SpringBoard line before 13:27 ---')
for i, line in sb:
    if '13:26:5' in line or '13:27:0' in line:
        print(i, line[:170])
