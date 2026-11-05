import json, sys
from collections import Counter, defaultdict
from datetime import datetime
runs = json.load(open(sys.argv[1], encoding='utf-8'))
runs = [r for r in runs if r.get('epoch')]
runs.sort(key=lambda r: r['epoch'])
print('dated runs:', len(runs))
byday = Counter(r['local'][:10] for r in runs)
print('by day:', dict(sorted(byday.items())))
ks = Counter(r['kernel'] for r in runs if r.get('kernel'))
print('\nkernel sizes:')
for size, n in ks.most_common(12):
    rows = [r for r in runs if r.get('kernel') == size]
    print(f'  {size} x{n}  {rows[0]["local"][:16]} .. {rows[-1]["local"][:16]}  kver={rows[0].get("kver","")[-28:]}')
rs = Counter(r['ramdisk'] for r in runs if r.get('ramdisk'))
print('\nramdisk sizes:', dict(rs.most_common(8)))
print('\neras:')
for label, cond in [
    ('redwood? <11-03', lambda r: r['local'] < '2026-11-03'),
    ('pclm10 bring-up 11-03/04', lambda r: '2026-11-03' <= r['local'] < '2026-11-04T19:00'),
    ('4 MiB touch era 11-04 19:00-23:00', lambda r: '2026-11-04T19:00' <= r['local'] < '2026-11-04T23:00'),
    ('GL era 11-05 01:00-09:00', lambda r: '2026-11-05T01:00' <= r['local'] < '2026-11-05T09:00'),
    ('interactive 11-05 09:00-12:00', lambda r: '2026-11-05T09:00' <= r['local'] < '2026-11-05T12:00'),
]:
    rows = [r for r in runs if cond(r)]
    if rows:
        print(f'  {label}: {len(rows)} runs, {rows[0]["local"][:16]}..{rows[-1]["local"][:16]}, ramdisk {sorted(set(r["ramdisk"] for r in rows if r.get("ramdisk")))}')
