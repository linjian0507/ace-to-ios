#!/usr/bin/env python3
"""Compare selected files inside two local APFS images; no device or image writes."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

spec = importlib.util.spec_from_file_location('apfs_inventory', Path(__file__).parent / 'apfs/apfs.py')
apfs = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = apfs
spec.loader.exec_module(apfs)


def inspect(path, files):
    container = apfs.Container(str(path))
    try:
        volume = apfs.pick_volume(container, None)
        result = {}
        if files is None:
            hit = volume.lookup('/')
            files = [name for name, _, fid, _ in apfs.walk(volume, hit[0], '')
                     if volume.inode(fid) is not None and
                     (volume.inode(fid).is_reg or volume.inode(fid).is_link)]
        for name in files:
            hit = volume.lookup(name)
            if hit is None:
                result[name] = None
            else:
                body = volume.inode(hit[0]).contents()
                result[name] = dict(size=len(body), sha256=hashlib.sha256(body).hexdigest())
        return result
    finally:
        container.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('old', type=Path)
    parser.add_argument('current', type=Path)
    parser.add_argument('--all-files', action='store_true', help='compare all regular files and symlink contents')
    args = parser.parse_args()
    files = ['/lib/' + name for name in ('libhakios.dylib', 'libhakios.sb.dylib', 'libhakios.gl.dylib', 'libOSMesa.8.dylib')]
    files += ['/etc/rc.jobs', '/LaunchDaemons/com.apple.sleepd.plist', '/LaunchDaemons/com.apple.chronod.plist']
    if args.all_files:
        files = None
    old, current = inspect(args.old, files), inspect(args.current, files)
    names = sorted(set(old) | set(current))
    rows = [dict(path=name, old=old.get(name), current=current.get(name),
                 identical=old.get(name) == current.get(name)) for name in names]
    report = dict(old=str(args.old), current=str(args.current), coverage='file contents and symlink targets; not metadata, directories or device nodes',
                  total=len(rows), identical=sum(row['identical'] for row in rows), files=rows)
    print(json.dumps(report, ensure_ascii=False, indent=2))
