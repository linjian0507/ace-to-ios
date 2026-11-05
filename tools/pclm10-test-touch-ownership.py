#!/usr/bin/env python3
"""Test the actual gauge control-scan gate: live touch must own its bus/reset."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'devices/pclm10/drivers/power/hakios.bq27541.c').read_text()
start = source.index('static void\ncontrol_scan(void)')
boundary = source.index('\n\t(void)bus_scan(', start)
gate = source[start:boundary]
assert 'gpio54/55/56' in gate
assert gate.count('PE_parse_boot_argn("touch"') == 1
prefix = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
static uint32_t requested;
static bool present;
static unsigned forbidden_actions;
static bool PE_parse_boot_argn(const char *name, void *out, size_t size) {
    assert(!strcmp(name, "touch") && size == sizeof(uint32_t));
    if (present) memcpy(out, &requested, size);
    return present;
}
#define say printf
'''
suffix = r'''
    forbidden_actions++;
}
int main(void) {
    const uint32_t levels[] = {0,1,2,UINT32_MAX};
    for (unsigned i = 0; i < 4; i++) {
        requested = levels[i]; present = true; forbidden_actions = 0;
        control_scan();
        assert(forbidden_actions == (requested == 0 ? 1u : 0u));
    }
    present = false; requested = 1; forbidden_actions = 0;
    control_scan();
    assert(forbidden_actions == 1);
    puts("PASS: 5 touch-ownership gate cases; no diagnostic bus/reset action while touch enabled");
}
'''
with tempfile.TemporaryDirectory(prefix='touch-ownership-') as directory:
    path = Path(directory)
    (path / 'test.c').write_text(prefix + gate + suffix)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
