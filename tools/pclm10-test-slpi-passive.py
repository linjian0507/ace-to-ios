#!/usr/bin/env python3
"""Test M21's actual passive observer with a deterministic monotonic clock."""

from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'devices/pclm10/drivers/wifi/hakios.slpi.c').read_text()
start = source.index('static void\nslpi_glink_passive(uint32_t wait_seconds)')
end = source.index('\n}\n', start) + 3
function = source[start:end]
for forbidden in ('smem_poke(', 'glink_mark(', 'glink_pump(', 'glink_say(', 'glink_kick('):
    assert forbidden not in function, forbidden
prefix = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#define NSEC_PER_SEC 1000000000ull
static uint64_t ticks;
static unsigned reports, samples;
static uint32_t marks[4] = {0, 0, 0, 8};
static struct {uint32_t descriptor, said;} glink = {.descriptor = 100, .said = 0};
static uint64_t mach_absolute_time(void) { return ticks; }
static void clock_interval_to_absolutetime_interval(uint32_t count, uint64_t scale, uint64_t *value) {
    assert(scale == NSEC_PER_SEC);
    *value = (uint64_t)count * 1000;
}
static void sleep_ms(uint32_t ms) { ticks += ms; }
static bool smem_peek(uint64_t at, void *to, uint32_t size) {
    assert(at == 100 && size == sizeof(marks));
    memcpy(to, marks, size);
    samples++;
    return true;
}
static void slpi_crash_report(const char *when) {
    assert(!strcmp(when, "M21 passive observation"));
    assert(glink.said == 0);
    reports++;
}
'''
suffix = r'''
int main(void) {
    const uint32_t durations[] = {0, 1, 45};
    for (unsigned i = 0; i < 3; i++) {
        ticks = samples = reports = 0;
        slpi_glink_passive(durations[i]);
        assert(ticks >= (uint64_t)durations[i] * 1000);
        assert(ticks <= (uint64_t)durations[i] * 1000 + 100);
        assert(samples == reports && reports >= 1);
        assert(marks[0] == 0 && marks[1] == 0 && marks[2] == 0 && marks[3] == 8);
        assert(glink.said == 0);
    }
    assert(reports == 10);
    puts("PASS: M21 0/1/45-real-second deadlines; ten 45s samples, no GLINK writes or transmissions");
}
'''
function = function.replace('(now - began) * 1000 / second,', '(unsigned long long)((now - began) * 1000 / second),')
with tempfile.TemporaryDirectory(prefix='m21-passive-') as directory:
    path = Path(directory)
    (path / 'test.c').write_text(prefix + function + suffix)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
