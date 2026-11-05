#!/usr/bin/env python3
"""Test the gauge's plausibility gate for real: what the charger is told about the pack.

gauge_plausible is the barrier between the gauge's answers and the charger driver: an
implausible pair must be refused rather than reported as the battery's charge.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'devices/pclm10/drivers/power/hakios.bq27541.c').read_text()
start = source.index('gauge_plausible(uint16_t soc, uint16_t volt)')
start = source.rindex('\n', 0, start) + 1
block_start = source.rindex('static bool', 0, start)
end = source.index('}', source.index('return soc <= 100', block_start)) + 1
func = source[block_start:end]
assert 'soc <= 100' in func and 'volt >= 2500' in func and 'volt <= 5000' in func

harness = r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
''' + func + r'''
int main(void) {
    // the whole plausible band
    assert(gauge_plausible(0, 2500));
    assert(gauge_plausible(100, 5000));
    assert(gauge_plausible(50, 3700));
    // each edge refused just outside it
    assert(!gauge_plausible(101, 3700));
    assert(!gauge_plausible(50, 2499));
    assert(!gauge_plausible(50, 5001));
    // a gauge that never answers cannot pass the gate
    assert(!gauge_plausible(0, 0));
    printf("PASS: 7 gauge plausibility cases; band and both edges\n");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='gauge-plausible-test-') as tmp:
    path = Path(tmp)
    (path / 'test.c').write_text(harness)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
