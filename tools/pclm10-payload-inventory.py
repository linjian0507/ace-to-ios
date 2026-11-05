#!/usr/bin/env python3
"""Read an XNUP payload and inventory its exact embedded artifacts, without device IO.

Optional extraction uses a NEW directory only; never overwrites a recovered file.
The format follows xnuloader/mkpayload.py, not Android boot-image heuristics.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


def parse(data):
    if len(data) < 32:
        raise ValueError('truncated XNUP header')
    magic, length = struct.unpack_from('<II', data)
    if magic != 0x50554E58 or length != len(data) - 8:
        raise ValueError('bad XNUP magic or declared length')
    entries = []
    names = set()
    offset = 8
    while offset + 24 <= len(data):
        raw_name, size, reserved = struct.unpack_from('<16sII', data, offset)
        offset += 24
        if raw_name == bytes(16):
            if size or reserved or offset != len(data):
                raise ValueError('invalid XNUP terminator/trailing bytes')
            return entries
        name = raw_name.split(b'\0', 1)[0].decode('ascii')
        if not re.fullmatch(r'[A-Za-z0-9_.-]+', name) or name in ('.', '..') or name in names:
            raise ValueError('unsafe or duplicate entry name')
        names.add(name)
        padded = (size + 15) & ~15
        if reserved or offset + padded > len(data):
            raise ValueError('truncated entry or nonzero reserved field')
        if any(data[offset + size:offset + padded]):
            raise ValueError('nonzero padding')
        entries.append((name, offset, data[offset:offset + size]))
        offset += padded
    raise ValueError('missing XNUP terminator')


def inventory(path, extract_to=None):
    data = path.read_bytes()
    entries = parse(data)  # Validate completely before creating any output.
    report = dict(payload=str(path), size=len(data), sha256=hashlib.sha256(data).hexdigest(), entries=[])
    for name, offset, body in entries:
        row = dict(name=name, offset=offset, size=len(body), sha256=hashlib.sha256(body).hexdigest())
        if name in ('cmdline', 'kernel.ver'):
            row['text'] = body.rstrip(b'\0').decode('utf-8', errors='replace')
        report['entries'].append(row)
    if extract_to is not None:
        extract_to.mkdir(parents=True, exist_ok=False)
        for name, _, body in entries:
            with (extract_to / name).open('xb') as output:
                output.write(body)
        with (extract_to / 'inventory.json').open('x', encoding='utf-8') as output:
            json.dump(report, output, ensure_ascii=False, indent=2)
            output.write('\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('payload', type=Path)
    parser.add_argument('--extract-to', type=Path)
    args = parser.parse_args()
    print(json.dumps(inventory(args.payload, args.extract_to), ensure_ascii=False, indent=2))
