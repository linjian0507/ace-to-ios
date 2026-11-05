#!/usr/bin/env python3
"""Test the WLED module-enable policy for real: lit/unlit writes and the refusals.

wled_apply is what the backlight thread calls when screen_backlight changes; the SPMI write
is mocked, so what is asserted is the register value it would write and the shadow it keeps.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'devices/pclm10/drivers/display/hakios.wled.c').read_text()
defines = []
for name in ('WLED_MOD_ENABLE', 'WLED_MODULE_EN_SHIFT'):
    line = source[source.index('#define ' + name):]
    defines.append(line[:line.index('\n')])
start = source.index('wled_apply(int lit)')
block_start = source.rindex('static const char *', 0, start)
end = source.index('\n}', start) + 2
func = source[block_start:end]
assert 'spmi_write' in func and 'wled_mod = value' in func

harness = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
''' + '\n'.join(defines) + r'''
static uint32_t wled_channel = 0;
static uint8_t wled_mod;
static int writes;
static uint8_t last_reg, last_val;
static const char *failure;
static const char *spmi_write(uint8_t reg, uint8_t value) {
    writes++; last_reg = reg; last_val = value;
    return failure;
}
''' + func + r'''
int main(void) {
    // no channel: refused, nothing written
    wled_channel = ~0u; wled_mod = 0; writes = 0;
    assert(strcmp(wled_apply(1), "no channel") == 0 && writes == 0);
    // lit: the module-enable bit is set and written
    wled_channel = 3; wled_mod = 0; writes = 0; failure = NULL;
    assert(wled_apply(1) == NULL);
    assert(writes == 1 && last_reg == WLED_MOD_ENABLE);
    assert(last_val == (1u << WLED_MODULE_EN_SHIFT) && wled_mod == last_val);
    // unlit: the same bit cleared
    assert(wled_apply(0) == NULL);
    assert(last_val == 0 && wled_mod == 0);
    // a refused write is reported and the shadow is not updated
    failure = "arbiter refused"; wled_mod = (1u << WLED_MODULE_EN_SHIFT);
    assert(strcmp(wled_apply(0), "arbiter refused") == 0);
    assert(wled_mod == (1u << WLED_MODULE_EN_SHIFT));
    printf("PASS: 4 wled module-enable cases; no channel, lit, unlit, refused write\n");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='wled-test-') as tmp:
    path = Path(tmp)
    (path / 'test.c').write_text(harness)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
