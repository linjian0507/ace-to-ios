import sys
from pathlib import Path
for name in sys.argv[1:]:
    p = Path(name)
    lines = p.read_text(errors='replace').splitlines()
    print('=' * 20, name, '=' * 20)
    in_conf = False
    count = 0
    for i, line in enumerate(lines, 1):
        if line.startswith('<<<<<<<'):
            in_conf = True; count += 1
            print(f'--- hunk {count} at line {i} ---')
        if in_conf:
            print(line[:200])
        if line.startswith('>>>>>>>'):
            in_conf = False
            print()
