import subprocess, sys
ours = subprocess.check_output(['git','show','pclm10-2026-11-05:configs/symbols.tsv']).decode('utf-8','replace').splitlines()
up = subprocess.check_output(['git','show','origin/main:configs/symbols.tsv']).decode('utf-8','replace').splitlines()
def bysym(lines):
    d = {}
    for l in lines:
        if not l or l.startswith('#'): continue
        d[l.split('\t')[0]] = l
    return d
o, u = bysym(ours), bysym(up)
for name in ['CAENV','METAL','SOC_SMEM','TOUCH_LAT']:
    print('==', name)
    print(' ours:', o.get(name))
    print(' up  :', u.get(name))
print('ours only count', len(set(o)-set(u)), 'up only', len(set(u)-set(o)), 'common', len(set(o)&set(u)))
