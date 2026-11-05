#!/usr/bin/env python3
"""Index existing PCLM10 run evidence without building, booting or touching a phone.

Sizes and shared version labels are not binary identity. A match to a saved
XNUP additionally requires the exact embedded command line, including epoch.
Touch maxima summarize logged observations, not user acceptance.
"""
import argparse
from datetime import datetime, timezone
import importlib.util
import json
from pathlib import Path
import re

spec = importlib.util.spec_from_file_location('history_payload', Path(__file__).with_name('pclm10-payload-inventory.py'))
payload_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(payload_module)


def inspect_log(path):
    text = path.read_text(errors='replace')
    lines = text.splitlines()
    entries = {}
    command = None
    command_line = None
    renderers = set()
    contacts = []
    faults = []
    root_size = None
    for number, line in enumerate(lines, 1):
        hit = re.match(r"payload entry '([^']+)' size ([0-9a-f]+)", line)
        if hit:
            entries[hit[1]] = int(hit[2], 16)
        if line.startswith('cmdline: debug='):
            command = line.removeprefix('cmdline: ')
            command_line = number
        hit = re.search(r'resident image .* size ([0-9a-f]+):', line)
        if hit:
            root_size = int(hit[1], 16)
        hit = re.search(r'(\d+) downs, (\d+) moves, (\d+) ups, (\d+) reads failed', line)
        if hit:
            contacts.append((tuple(map(int, hit.groups())), number))
        if 'context for CoreAnimation' in line:
            if 'softpipe' in line:
                renderers.add('softpipe')
            elif 'llvmpipe' in line:
                renderers.add('llvmpipe')
            else:
                hit = re.search(r';\s*(FD\d+|Adreno[^;]*)', line)
                if hit:
                    renderers.add(hit[1])
        if 'no GPU draws' in line:
            renderers.add('no GPU draws (explicit log)')
        if ('THE SMMU REFUSED the 3D core' in line or 'adreno: gpu: no answer:' in line
                or 'the GPU is not done with surface' in line):
            # Keep the evidence only. Teardown ordering needs full-log review.
            if len(faults) < 3:
                faults.append(dict(line=number, text=line))
    epoch_hit = re.search(r'(?:^| )epoch=(\d+)(?: |$)', command or '')
    epoch = int(epoch_hit[1]) if epoch_hit else None
    best = max(contacts, default=None)
    return dict(log=path.name, epoch=epoch,
                utc=datetime.fromtimestamp(epoch, timezone.utc).isoformat() if epoch else None,
                payload_sizes=entries, resident_size=root_size, command=command, command_line=command_line,
                renderers=sorted(renderers),
                touch_max=list(best[0]) if best else None, touch_line=best[1] if best else None,
                fault_samples=faults,
                returned='kernel returned:' in text,
                truncated='the log between the first' in text)


def bind(saved_payload, rows):
    entries = {name: body for name, _, body in payload_module.parse(saved_payload.read_bytes())}
    command = entries.get('cmdline', b'').rstrip(b'\0').decode('utf-8')
    sizes = {name: len(body) for name, body in entries.items()}
    matches = []
    for row in rows:
        if (command and (row['command'] or '').endswith(' ' + command)
                and all(row['payload_sizes'].get(name) == size for name, size in sizes.items())):
            matches.append(row['log'])
    return dict(payload=str(saved_payload), matches=matches,
                rule='exact payload command with epoch + all entry sizes; not runtime-memory hash proof')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', type=Path)
    parser.add_argument('--payload', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    rows = [inspect_log(path) for path in sorted(args.logs.glob('*-run.log'))]
    rows.sort(key=lambda row: (row['epoch'] or 0, row['log']))
    report = dict(coverage='complete named run-log files in this directory; no live snapshots or user-chat history',
                  run_count=len(rows), runs=rows)
    if args.payload:
        report['payload_binding'] = bind(args.payload, rows)
    with args.output.open('x', encoding='utf-8') as output:
        json.dump(report, output, ensure_ascii=False, indent=2)
        output.write('\n')
    print('Indexed', len(rows), 'existing run logs; output', args.output)
    if args.payload:
        print('Exact saved-payload candidates:', ', '.join(report['payload_binding']['matches']) or 'none')
    for row in rows:
        if row['payload_sizes'].get('kernel') == 0xe8ab90 or row['log'] in ('014918-run.log', '200228-run.log'):
            print(row['utc'], row['log'], row['payload_sizes'], 'touch', row['touch_max'], 'renderer', row['renderers'])
