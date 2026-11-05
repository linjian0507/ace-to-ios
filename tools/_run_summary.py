import re, sys
from pathlib import Path
for name in sys.argv[1:]:
    p = Path(name)
    if not p.exists():
        print(name, 'MISSING'); continue
    lines = p.read_text(errors='replace').splitlines()
    teardown = None; first_frame = None; stats = []; tmax = None; touch = None
    for i, line in enumerate(lines, 1):
        m = re.search(r'\[(\d+\.\d+)\]: adreno: the run is about to end', line)
        if m: teardown = float(m.group(1))
        m = re.search(r'\[(\d+\.\d+)\]: MiniIOMobileFramebuffer: .*frames in', line)
        if m:
            if first_frame is None: first_frame = float(m.group(1))
            stats.append(m.group(1))
        m = re.search(r'thermal: (\d+) of (\d+) sensors read; hottest [^ ]+ at ([\d.]+) C', line)
        if m: tmax = max(tmax or 0, float(m.group(3)))
        m = re.search(r'(\d+) downs, (\d+) moves, (\d+) ups, (\d+) reads failed', line)
        if m: touch = m.groups()
    # run end from last timestamp
    last = None
    for line in reversed(lines):
        m = re.search(r'\[(\d+\.\d+)\]', line)
        if m: last = float(m.group(1)); break
    print('===', p.name)
    print('  last ts', last, 'teardown', teardown, 'first frame stats', first_frame)
    print('  thermal max', tmax, 'touch', touch, 'stat lines', len(stats))
