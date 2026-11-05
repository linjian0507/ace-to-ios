import re, sys
from pathlib import Path

logs = sys.argv[1:]
for name in logs:
    p = Path(name)
    if not p.exists():
        print(name, 'MISSING'); continue
    text = p.read_text(errors='replace').splitlines()
    teardown = None
    first_frame = None
    stats = []
    context = None
    for i, line in enumerate(text, 1):
        m = re.search(r'\[(\d+\.\d+)\]: (MiniIOMobileFramebuffer: .*frames in .*)', line)
        if m:
            ts = float(m.group(1))
            stats.append((ts, m.group(2)[:120]))
            if first_frame is None: first_frame = ts
        m = re.search(r'\[(\d+\.\d+)\]: adreno: the run is about to end', line)
        if m: teardown = float(m.group(1))
        if 'context for CoreAnimation' in line and context is None:
            context = (i, line[:120])
    start = (teardown - 158) if teardown else None
    print('===', p.name, '===')
    print('teardown', teardown, 'approx start', round(start,1) if start else None)
    if stats:
        print('first frame stats at', stats[0][0], '=> seconds into run', round(stats[0][0]-start,1) if start else '?')
        for ts, s in stats[:8]:
            print('  ', ts, round(ts-start,1) if start else '?', s)
    else:
        print('no frame stats')
    print('context line', context)
