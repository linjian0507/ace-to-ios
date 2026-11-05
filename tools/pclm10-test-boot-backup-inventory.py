#!/usr/bin/env python3
"""Test CRC/bounds checks for offline HKUB backup inventory."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

spec = importlib.util.spec_from_file_location('boot_inventory', Path(__file__).with_name('pclm10-inventory-boot-backups.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def image(body=b'loader bytes', crc=None, offset=4096):
    table = struct.pack('<16sIIII', b'loader', offset, len(body), zlib.crc32(body) if crc is None else crc, 0)
    head = struct.pack('<4sIII', b'HKUB', 1, 1, 48) + table
    return b'prefix' + head + bytes(4096-len(head)) + body


class BootTests(unittest.TestCase):
    def scan(self, data):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'boot.img'
            path.write_bytes(data)
            return module.scan(path)

    def test_valid_embedded_container(self):
        report = self.scan(image())
        self.assertEqual(report['containers'][0]['offset'], 6)
        self.assertTrue(report['containers'][0]['entries'][0]['crc_valid'])

    def test_bad_crc_rejected(self):
        self.assertEqual(self.scan(image(crc=0))['containers'], [])

    def test_truncation_rejected(self):
        self.assertEqual(self.scan(image()[:-1])['containers'], [])

    def test_unaligned_or_out_of_range_rejected(self):
        for offset in (48, 4097, 8192):
            self.assertEqual(self.scan(image(offset=offset))['containers'], [])


if __name__ == '__main__':
    unittest.main()
