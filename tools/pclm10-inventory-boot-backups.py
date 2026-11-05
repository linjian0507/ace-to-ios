#!/usr/bin/env python3
"""Inventory validated HKUB containers inside local boot backups. No device IO."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import zlib

spec = importlib.util.spec_from_file_location('xnup_inventory', Path(__file__).with_name('pclm10-payload-inventory.py'))
xnup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(xnup)


def scan(path):
    data = path.read_bytes()
    report = dict(path=str(path), size=len(data), sha256=hashlib.sha256(data).hexdigest(), containers=[])
    at = data.find(b'HKUB')
    while at >= 0:
        try:
            magic, version, count, header = struct.unpack_from('<4sIII', data, at)
            if version != 1 or not 0 < count <= 16 or header != 16 + count * 32:
                raise ValueError('not a valid header')
            entries = []
            spans = []
            for i in range(count):
                raw, offset, size, crc, reserved = struct.unpack_from('<16sIIII', data, at + 16 + i * 32)
                name = raw.split(b'\0', 1)[0].decode('ascii')
                if reserved or offset < header or offset % 4096 or at + offset + size > len(data):
                    raise ValueError('invalid entry bounds')
                if any(offset < end and offset + size > start for start, end in spans):
                    raise ValueError('overlapping entries')
                spans.append((offset, offset + size))
                body = data[at + offset:at + offset + size]
                if zlib.crc32(body) != crc:
                    raise ValueError('bad CRC')
                row = dict(name=name, offset=at+offset, size=size, sha256=hashlib.sha256(body).hexdigest(), crc_valid=True)
                if name == 'payload':
                    row['xnup'] = []
                    for item, _, content in xnup.parse(body):
                        entry = dict(name=item, size=len(content), sha256=hashlib.sha256(content).hexdigest())
                        if item in ('cmdline', 'kernel.ver'):
                            entry['text'] = content.rstrip(b'\0').decode('utf-8', errors='replace')
                        row['xnup'].append(entry)
                entries.append(row)
            report['containers'].append(dict(offset=at, entries=entries))
        except (ValueError, UnicodeDecodeError, struct.error):
            pass
        at = data.find(b'HKUB', at + 4)
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('images', nargs='+', type=Path)
    args = parser.parse_args()
    print(json.dumps([scan(path) for path in args.images], ensure_ascii=False, indent=2))
