#!/usr/bin/env python3
"""Verify a recovered original XNUP bundle against its inventory and historical log.

Read-only host validation. Does not run xl/fastboot, rebuild a payload, mutate
the bundle, certify resident-root identity or pronounce user acceptance.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re

spec = importlib.util.spec_from_file_location('verify_payload', Path(__file__).with_name('pclm10-payload-inventory.py'))
payload_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(payload_module)


def verify(bundle, log):
    inventory = json.loads((bundle/'inventory.json').read_text(encoding='utf-8'))
    original = (bundle/'original-payload.bin').read_bytes()
    errors = []
    if len(original) != inventory['size'] or hashlib.sha256(original).hexdigest() != inventory['sha256']:
        errors.append('original payload does not match archived size/SHA256')
    entries = payload_module.parse(original)
    actual = {name: body for name, _, body in entries}
    expected = {row['name']: row for row in inventory['entries']}
    if set(actual) != set(expected):
        errors.append('payload entry set differs from inventory')
    for name, offset, body in entries:
        row = expected.get(name, {})
        if (row.get('offset') != offset or row.get('size') != len(body)
                or row.get('sha256') != hashlib.sha256(body).hexdigest()):
            errors.append('inventory mismatch: ' + name)
        extracted = bundle/name
        if not extracted.is_file() or extracted.read_bytes() != body:
            errors.append('missing/modified extracted original: ' + name)
    log_text = log.read_text(errors='replace')
    sizes = {name: int(size, 16) for name, size in
             re.findall(r"^payload entry '([^']+)' size ([0-9a-f]+)$", log_text, re.M)}
    for name, _, body in entries:
        if sizes.get(name) != len(body):
            errors.append('historical log entry size mismatch: ' + name)
    command = actual.get('cmdline', b'').rstrip(b'\0').decode('utf-8')
    commands = [line.removeprefix('cmdline: ') for line in log_text.splitlines()
                if line.startswith('cmdline: debug=')]
    if not command or not any(line.endswith(' ' + command) for line in commands):
        errors.append('historical command/epoch mismatch')
    return dict(bundle=str(bundle), log=str(log), errors=errors,
                original_verified=not errors,
                scope='archived original and log correspondence only; resident root, live loader and usability NOT certified')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundle', type=Path)
    parser.add_argument('log', type=Path)
    args = parser.parse_args()
    result = verify(args.bundle, args.log)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    raise SystemExit(bool(result['errors']))
