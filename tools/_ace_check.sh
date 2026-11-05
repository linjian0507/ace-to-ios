#!/bin/bash
timeout 40 curl -s "https://api.github.com/repos/linjian0507/ace-to-ios/commits?per_page=3" -o /tmp/ace.json
python3 - <<'PY'
import json
try:
    d=json.load(open('/tmp/ace.json'))
    for c in d:
        print(c['sha'][:8], c['commit']['committer']['date'], c['commit']['message'].split(chr(10))[0])
except Exception as e:
    print('api failed', e)
PY
