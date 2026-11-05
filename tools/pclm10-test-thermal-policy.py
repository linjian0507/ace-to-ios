#!/usr/bin/env python3
"""Test the thermal stop policy for real: three readings at or above thermal_stop stop the run.

The block is the one hakios.thermal.c runs on every poll (the strikes and the stop), compiled
with the driver's calls mocked; the boot argument's clamp is checked in the source it is parsed in.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'xnu-src/xnu-8796.121.2/bsd/dev/hakios.thermal.c').read_text()
start = source.index('if (stop_celsius != 0 && hottest >= (int32_t)stop_celsius * 10) {')
lr = source.index('loader_return();', start)
end = source.index('}', lr + len('loader_return();')) + 1
block = source[start:end]
assert 'strikes++' in block and 'loader_return' in block
# the clamp where thermal_stop is parsed: 0..160 only
assert 'value <= 160' in source[source.index('PE_parse_boot_argn("thermal_stop"'):]
assert 'stop_celsius = value;' in source

prefix = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
static uint32_t stop_celsius = 105;
static int strikes;
static bool stopping;
static int stops, saves;
static void say(const char *format, ...) { (void)format; }
static void text_of(char *out, size_t size, int32_t tenths) { snprintf(out, size, "%d", (int)tenths); }
static void hakios_var_save_on_return(void) { saves++; }
static void hakios_adreno_stop(void) {}
static void hakios_wifi_stop(void) {}
static void hakios_adsp_stop(void) {}
static void loader_return(void) { stops++; }
struct sensor { const char *name; };
static struct sensor sensors[4] = {{"s0"}, {"s1"}, {"s2"}, {"s3"}};
static void step(int32_t hottest, uint32_t hot) {
    char one[16];
'''
suffix = r'''
}
int main(void) {
    // the boundary: at the stop (105.0) is a strike, 104.9 is not
    stop_celsius = 105; strikes = 0; stopping = false; stops = 0;
    step(1049, 0); assert(strikes == 0);
    step(1050, 0); assert(strikes == 1);
    step(1050, 0); assert(strikes == 2); assert(stops == 0);
    step(1050, 0); assert(strikes == 3); assert(stops == 1);
    // a reading below resets the run
    stop_celsius = 80; strikes = 2; stopping = false; stops = 0;
    step(799, 0); assert(strikes == 0);
    stop_celsius = 80; strikes = 0; stopping = false; stops = 0;
    step(800, 0); assert(strikes == 1);
    // thermal_stop=0 turns the stop off
    stop_celsius = 0; strikes = 0; stopping = false; stops = 0;
    step(2000, 0); assert(strikes == 0 && stops == 0);
    printf("PASS: 6 thermal stop-policy cases; three strikes, reset, boundary, off\n");
    return 0;
}
'''
harness = prefix + block + suffix
with tempfile.TemporaryDirectory(prefix='thermal-policy-test-') as tmp:
    path = Path(tmp)
    (path / 'test.c').write_text(harness)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
