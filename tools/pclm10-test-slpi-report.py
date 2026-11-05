#!/usr/bin/env python3
"""Host-only regression tests for the SLPI evidence summary helper."""

import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest


spec = importlib.util.spec_from_file_location(
    'slpi_report', Path(__file__).with_name('pclm10-slpi-report.py')
)
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


class ReportTests(unittest.TestCase):
    def summary(self, text):
        with tempfile.TemporaryDirectory(prefix='slpi-report-') as directory:
            path = Path(directory) / 'run.log'
            path.write_text(text)
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                report.summarize(path)
            return output.getvalue()

    def test_peak_uses_complete_log_and_does_not_count_threshold(self):
        result = self.summary(
            'thermal: stop at 60 C\n'
            'thermal: hottest cpu-0-0 at 42.0 C\n'
            'thermal: hottest cpuss-0 at 48.2 C\n'
            'thermal: hottest cpu-0-0 at 41.7 C\n'
        )
        self.assertIn('48.2 C [line 3]', result)
        self.assertIn('Thermal-stop text candidates: 0', result)

    def test_missing_record_is_not_zero(self):
        result = self.summary('partial log\n')
        self.assertIn('Return marker: absent', result)
        self.assertIn('peak: unavailable', result)
        self.assertIn('do not infer zero', result)
        self.assertNotIn('dispatch 0', result)

    def test_counts_deduplicate_and_preserve_ids(self):
        line = ('slpi: m16: at the end: the gic\'s dispatch has taken spi 202 2 times '
                'and spi 204 1 times; our own handler was entered 2 and 1 times')
        result = self.summary(line + '\n[123.00]: ' + line + '\nkernel returned: log\n')
        self.assertEqual(result.count('INTID 202:'), 1)
        self.assertIn('INTID 204: dispatch 1, handler 1', result)
        self.assertIn('Return marker: present', result)
        self.assertNotIn('WARNING', result)

    def test_mismatched_counts_warn(self):
        result = self.summary(
            "the gic's dispatch has taken spi 202 3 times and spi 204 1 times; "
            'our own handler was entered 2 and 1 times\n'
        )
        self.assertIn('WARNING: dispatcher and handler counts differ', result)

    def test_partial_count_record_not_treated_as_complete(self):
        result = self.summary("the gic's dispatch has taken spi 202 2 times\n")
        self.assertIn('No complete interrupt count records', result)

    def test_ack_and_protocol_evidence_have_line_numbers(self):
        result = self.summary(
            'slpi: m18: item 430 flags 0 -> 0x2, readback 0x2\n'
            'slpi: m4: after 45 s: 6 messages sent, 1 heard\n'
            'slpi: m4: the descriptor at smem+0x17f300 reads: our queue read to 0 and written to 48\n'
            'thermal: stopping because too hot\n'
        )
        self.assertIn('[line 1] slpi: m18:', result)
        self.assertIn('[line 2] slpi: m4: after', result)
        self.assertIn('[line 3] slpi: m4: the descriptor', result)
        self.assertIn('Thermal-stop text candidates: 1', result)

    def test_secure_shutdown_evidence_is_preserved(self):
        result = self.summary(
            'slpi: m19: PAS_SHUTDOWN transport 0, firmware answer 0\n'
            'slpi: m19: previous SLPI shutdown succeeded\n'
        )
        self.assertIn('[line 1] slpi: m19: PAS_SHUTDOWN transport 0', result)
        self.assertIn('[line 2] slpi: m19: previous SLPI shutdown succeeded', result)

    def test_actual_thermal_driver_stop_message(self):
        result = self.summary(
            '\n--- thermal: cpuss-0 reads 60.1 C, at or above thermal_stop=60 '
            'three times in a row: back to the loader ---\n'
        )
        self.assertIn('Thermal-stop text candidates: 1', result)
        self.assertIn('[line 2] --- thermal:', result)

    def test_m20_crash_observation_is_not_dropped(self):
        result = self.summary(
            'slpi: m20: at end of exchange: watchdog claimed 1, dispatch delta 0, '
            'handler 0; SFR readable 1, changed since shutdown baseline 0\n'
        )
        self.assertIn('[line 1] slpi: m20:', result)
        self.assertIn('changed since shutdown baseline 0', result)

    def test_m21_passive_cursors_are_preserved(self):
        result = self.summary(
            'slpi: m21: sample 1 at 5000 ms: host tail 0 head 0, '
            'remote tail 0 head 8; host messages sent 0\n'
        )
        self.assertIn('[line 1] slpi: m21: sample 1 at 5000 ms:', result)
        self.assertIn('host messages sent 0', result)


if __name__ == '__main__':
    unittest.main()
