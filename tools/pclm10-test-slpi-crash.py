#!/usr/bin/env python3
"""Run M20's actual crash-observation functions against read-only fake SMEM."""

from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'devices/pclm10/drivers/wifi/hakios.slpi.c').read_text()
start = source.index('#define SLPI_WDOG_INTID 526u')
end = source.index('/* ---- What the SLPI itself says:', start)
functions = source[start:end]
assert re.search(r'SLPI_WDOG_INTID 526u', functions)
assert 494 + 32 == 526
assert 'smem_poke(' not in functions and 'scm(' not in functions
prefix = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#define __unused __attribute__((unused))
#define ITEM_SLPI_SFR 424
#define SMP2P_FATAL 0
#define SMP2P_READY 1
#define KERN_SUCCESS 0
typedef int kern_return_t;
static uint8_t memory[256];
static uint32_t at = 32, item_size = 128, state;
static uint64_t interrupt_count;
static bool fail_read, valid_state = true;
static int attach_status;
static unsigned reads;
static char output[4096];
static size_t used;
static int capture(const char *format, ...) {
    va_list list;
    va_start(list, format);
    int n = vsnprintf(output + used, sizeof(output) - used, format, list);
    va_end(list);
    assert(n >= 0 && (size_t)n < sizeof(output) - used);
    used += (size_t)n;
    return n;
}
#define printf capture
static uint32_t smem_item(uint32_t item, uint32_t *size) {
    assert(item == 424);
    *size = item_size;
    return at;
}
static bool smem_peek(uint64_t offset, void *to, uint32_t size) {
    assert(size <= 96 && offset + size <= sizeof(memory));
    reads++;
    if (fail_read) return false;
    memcpy(to, memory + offset, size);
    return true;
}
static kern_return_t gic_v3_spi_attach(uint32_t intid, bool edge,
        void (*handler)(void *, uint32_t), void *context) {
    assert(intid == 526 && edge && handler && !context);
    return attach_status;
}
static uint64_t gic_v3_spi_count(uint32_t intid) {
    assert(intid == 526);
    return interrupt_count;
}
static bool slpi_smp2p_state(uint32_t *value, const char **why) {
    *value = state;
    *why = valid_state ? "the entry is there" : "no item";
    return valid_state;
}
'''
suffix = r'''
static void clear_output(void) { used = 0; output[0] = 0; }
int main(void) {
    uint8_t before[256];
    memset(memory, 0xa5, sizeof(memory));
    memcpy(before, memory, sizeof(memory));
    interrupt_count = 9;
    slpi_crash_baseline();
    assert(slpi_sfr_baseline_valid && slpi_sfr_baseline_size == 96);
    assert(slpi_wdog_claimed && slpi_wdog_baseline == 9);
    clear_output();
    slpi_crash_report("unchanged");
    assert(strstr(output, "dispatch delta 0, handler 0"));
    assert(strstr(output, "SFR readable 1, changed since shutdown baseline 0"));
    memory[39] = 0x34;
    clear_output();
    interrupt_count = 10;
    slpi_wdog_arrived(NULL, 526);
    state = 1;
    slpi_crash_report("new event");
    assert(strstr(output, "dispatch delta 1, handler 1"));
    assert(strstr(output, "changed since shutdown baseline 1"));
    assert(strstr(output, "first differing byte 7"));
    assert(strstr(output, "fatal 1, ready 0"));
    memory[39] = before[39];
    item_size = 4;
    clear_output();
    slpi_crash_report("shorter item");
    assert(strstr(output, "changed since shutdown baseline 1"));
    fail_read = true;
    clear_output();
    slpi_crash_report("unreadable");
    assert(strstr(output, "SFR readable 0, changed since shutdown baseline 0"));
    fail_read = false; at = 0; item_size = 128;
    attach_status = 7;
    slpi_crash_baseline();
    assert(!slpi_wdog_claimed && !slpi_sfr_baseline_valid);
    at = 64;
    clear_output();
    state = 2;
    slpi_crash_report("appeared later");
    assert(strstr(output, "SFR readable 1, changed since shutdown baseline 1"));
    assert(strstr(output, "fatal 0, ready 1"));
    assert(memcmp(before, memory, sizeof(memory)) == 0);
    assert(reads >= 5);
    puts("PASS: M20 unchanged/new/short/unreadable/absent/appeared crash evidence; no SMEM writes");
}
'''
with tempfile.TemporaryDirectory(prefix='m20-crash-') as directory:
    path = Path(directory)
    # Match host libc's uint64_t formatting while keeping driver logic intact.
    functions = functions.replace('kr, slpi_wdog_baseline);', 'kr, (unsigned long long)slpi_wdog_baseline);')
    functions = functions.replace('count - slpi_wdog_baseline, slpi_wdog_entered,', '(unsigned long long)(count - slpi_wdog_baseline), slpi_wdog_entered,')
    (path / 'test.c').write_text(prefix + functions + suffix)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
