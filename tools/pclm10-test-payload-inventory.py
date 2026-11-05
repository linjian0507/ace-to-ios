#!/usr/bin/env python3
"""Host-only tests for safe recovery of saved XNUP artifacts."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('payload_inventory', Path(__file__).with_name('pclm10-payload-inventory.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def payload(entries):
    body = b''
    for name, data in entries:
        body += struct.pack('<16sII', name.encode(), len(data), 0)
        body += data + bytes(-len(data) % 16)
    body += bytes(24)
    return struct.pack('<II', 0x50554e58, len(body)) + body


class PayloadTests(unittest.TestCase):
    def test_roundtrip(self):
        raw = payload([('kernel', b'original kernel'), ('ramdisk', b'original image'), ('cmdline', b'arg=1\0')])
        self.assertEqual([(name, body) for name, _, body in module.parse(raw)],
                         [('kernel', b'original kernel'), ('ramdisk', b'original image'), ('cmdline', b'arg=1\0')])

    def test_truncation(self):
        raw = payload([('kernel', b'abc')])
        for length in (0, 7, 20, len(raw)-1):
            with self.assertRaises(ValueError):
                module.parse(raw[:length])

    def test_names(self):
        for entries in ([('../kernel', b'a')], [('kernel', b'a'), ('kernel', b'b')], [('..', b'a')]):
            with self.assertRaises(ValueError):
                module.parse(payload(entries))

    def test_existing_recovery_directory_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            source = path/'payload.bin'
            source.write_bytes(payload([('kernel', b'original')]))
            dest = path/'recovery'
            module.inventory(source, dest)
            self.assertEqual((dest/'kernel').read_bytes(), b'original')
            with self.assertRaises(FileExistsError):
                module.inventory(source, dest)

    def test_invalid_payload_creates_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            source = path/'bad.bin'
            source.write_bytes(b'bad')
            dest = path/'recovery'
            with self.assertRaises(ValueError):
                module.inventory(source, dest)
            self.assertFalse(dest.exists())


if __name__ == '__main__':
    unittest.main()
