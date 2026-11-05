import re, sys
from pathlib import Path
for name in sys.argv[1:]:
    p = Path(name)
    text = p.read_text(errors='replace').splitlines()
    print('===', p.name, '===')
    n = 0
    for i, line in enumerate(text, 1):
        if 'com.apple.xpc.launchd|' in line and n < 4:
            print(' launchd', i, line[:150]); n += 1
        if 'SpringBoard created' in line or ('SpringBoard' in line and 'created' in line):
            print(' SB', i, line[:150])
        if re.search(r'(frame|swap)', line, re.I) and re.search(r'\d+ frames|swap \d+', line):
            print(' frame', i, line[:160])
    for i, line in enumerate(text, 1):
        if 'no GPU draws' in line or 'Software renderer' in line or 'Failed to initialize Metal' in line:
            print(' fallback', i, line[:160])
