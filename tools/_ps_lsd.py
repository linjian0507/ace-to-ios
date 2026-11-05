import re, sys
from pathlib import Path
p = Path(sys.argv[1])
lines = p.read_text(errors='replace').splitlines()
print('ps snapshots and lsd/SpringBoard rows:')
snap = 0
for i, line in enumerate(lines):
    if line.startswith('$ /bin/ps -A'):
        snap += 1
        print(f'--- snapshot {snap} at line {i+1} ---')
        for j in range(i, min(i+140, len(lines))):
            m = re.search(r'(\d+:\d+\.\d+)\s+(/usr/libexec/lsd|.*SpringBoard.*|.*backboardd.*)', lines[j])
            if m and ('lsd' in lines[j] or 'SpringBoard' in lines[j] or 'backboardd' in lines[j]):
                print('   ', lines[j][:130])
