import re, sys
from pathlib import Path
for name in sys.argv[1:]:
    p = Path(name)
    lines = p.read_text(errors='replace').splitlines()
    first_surface = None; first_frames = None; last = None
    for i, line in enumerate(lines, 1):
        m = re.match(r'\[\s*(\d+\.\d+)\]', line)
        if not m: continue
        ts = float(m.group(1)); last = ts
        if first_surface is None and 'SpringBoard[7] created surface' in line:
            first_surface = (ts, i)
        if first_frames is None and 'frames in' in line and 'MiniIOMobileFramebuffer' in line:
            first_frames = (ts, i)
    print(p.name, 'last', last, 'first surface', first_surface, 'first frame stats', first_frames)
