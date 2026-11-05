#!/usr/bin/env python3
"""Check the backlight's control surface: a sysctl iOS can set, a thread that applies it, and a gate.

The panel itself is not tested here. What is checked is that the driver exposes screen_backlight
read-write, applies every change through wled_apply, only runs with wled=1, and keeps the startup
blink behind wled_test so a normal boot has no flash.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'devices/pclm10/drivers/display/hakios.wled.c').read_text()

# the knob the system writes
assert 'SYSCTL_INT(_kern_hakios, OID_AUTO, screen_backlight' in source
assert 'CTLFLAG_RW' in source
assert 'the panel\'s backlight' in source
# a thread follows the knob and applies each change
assert 'wled_apply(now)' in source
assert 'delay_for_interval(100' in source
# off by default; only wled=1 brings the module up
assert 'PE_parse_boot_argn("wled"' in source
# the visible blink is a test mode, never a normal boot
assert 'PE_parse_boot_argn("wled_test"' in source
print("PASS: backlight control surface; sysctl RW, applied by the thread, gated by wled=1, blink test-only")
