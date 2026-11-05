import re, sys
from pathlib import Path
p = Path(sys.argv[1])
lines = p.read_text(errors='replace').splitlines()
start = None
for line in lines:
    m = re.search(r'epoch=(\d+)', line)
    if m and 'loader_return_after' in line:
        start = int(m.group(1)); break
markers = [('context', 'context for CoreAnimation'), ('sb_surface', 'SpringBoard[7] created surface'),
           ('frames', 'MiniIOMobileFramebuffer: '), ('hkgpu', 'HakiosGPU: submission'),
           ('swap1', 'MiniIOMobileFramebuffer: swap 1 ')]
seen = {}
for i, line in enumerate(lines, 1):
    m = re.match(r'\[\s*(\d+\.\d+)\]', line)
    if not m: continue
    ts = float(m.group(1))
    for key, pat in markers:
        if key not in seen and pat in line:
            seen[key] = (ts, i, line[:150])
for key, (ts, i, text) in seen.items():
    print(f'{key}: ts={ts} line={i} {text}')
print('start epoch local approx run start; first frames listed below')
n = 0
for i, line in enumerate(lines, 1):
    if 'frames in' in line and re.match(r'\[\s*\d+\.\d+\]', line):
        print(' frame', i, line[:140]); n += 1
        if n >= 6: break
