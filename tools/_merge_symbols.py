import subprocess
from pathlib import Path
def show(ref):
    return subprocess.check_output(['git','show',ref]).decode('utf-8','replace').splitlines()
ours = [l for l in show('pclm10-2026-11-05:configs/symbols.tsv') if l.strip()]
up = [l for l in show('origin/main:configs/symbols.tsv') if l.strip()]
merged = {l.split('\t')[0]: l for l in up}
added = 0
for l in ours:
    k = l.split('\t')[0]
    if k not in merged:
        merged[k] = l; added += 1
out = '\n'.join(merged[k] for k in sorted(merged)) + '\n'
Path('configs/symbols.tsv').write_text(out, encoding='utf-8', newline='\n')
print('upstream', len(up), 'ours', len(ours), 'merged', len(merged), 'added from ours', added)
