#!/usr/bin/env python3
"""Offline history/recovered-bundle tests. Uses synthetic files; no phone IO."""
import importlib.util
from pathlib import Path
import tempfile
import unittest


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


history = load('test_history', 'pclm10-history-index.py')
verifier = load('test_recovered', 'pclm10-verify-recovered.py')
fixtures = load('test_payload_fixtures', 'pclm10-test-payload-inventory.py')


class RecoveryTests(unittest.TestCase):
    command = 'loader_return_after=90 epoch=1793814558 bootcpu=7'

    def log(self, command=None):
        command = self.command if command is None else command
        return ("payload entry 'kernel' size 3\n"
                f"payload entry 'cmdline' size {len(command.encode())+1:x}\n"
                'cmdline: debug=0x8 rd=md0 ' + command + '\n'
                'touch: 100 passes: 0 downs, 0 moves, 0 ups, 0 reads failed\n'
                'touch: 900 passes: 26 downs, 459 moves, 26 ups, 0 reads failed\n')

    def setup_bundle(self, directory):
        root = Path(directory)
        raw = fixtures.payload([('kernel', b'abc'), ('cmdline', self.command.encode()+b'\0')])
        source = root/'saved.bin'
        source.write_bytes(raw)
        bundle = root/'recovered'
        verifier.payload_module.inventory(source, bundle)
        (bundle/'original-payload.bin').write_bytes(raw)
        log = root/'original-run.log'
        log.write_text(self.log())
        return bundle, log

    def test_exact_original(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle, log = self.setup_bundle(directory)
            self.assertTrue(verifier.verify(bundle, log)['original_verified'])

    def test_modified_extracted_kernel_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle, log = self.setup_bundle(directory)
            (bundle/'kernel').write_bytes(b'new')
            self.assertIn('missing/modified extracted original: kernel', verifier.verify(bundle, log)['errors'])

    def test_other_run_same_size_not_original(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle, log = self.setup_bundle(directory)
            log.write_text(self.log(self.command.replace('1793814558', '1793814559')))
            self.assertIn('historical command/epoch mismatch', verifier.verify(bundle, log)['errors'])

    def test_history_reads_contacts_at_end(self):
        with tempfile.TemporaryDirectory() as directory:
            _, log = self.setup_bundle(directory)
            row = history.inspect_log(log)
            self.assertEqual(row['touch_max'], [26,459,26,0])
            self.assertEqual(row['epoch'], 1793814558)

    def test_binding_requires_command_and_sizes(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle, log = self.setup_bundle(directory)
            row = history.inspect_log(log)
            result = history.bind(bundle/'original-payload.bin', [row])
            self.assertEqual(result['matches'], ['original-run.log'])
            row['payload_sizes']['kernel'] = 4
            self.assertEqual(history.bind(bundle/'original-payload.bin', [row])['matches'], [])


if __name__ == '__main__':
    unittest.main()
