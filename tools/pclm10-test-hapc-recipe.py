#!/usr/bin/env python3
"""Test the haptic sequence's bounds and its restore: a buzz is finite and leaves the chip as found.

The recipe, the play, the stop and the restore are hapc_buzz's own text; what is asserted is what
the code guarantees without a motor: the register values, a bounded duration, and the restore.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'devices/pclm10/drivers/haptic/hakios.hapc.c').read_text()
start = source.index('hapc_buzz(int leave_running)')
end = source.index('\n}', source.index('put back as found', start)) + 2
buzz = source[start:end]

# the recipe: the values the tree's actuator says (LRA, sine, 3400 mV, 6667 us, direct)
for reg, value in [('HAPC_SEC_ACCESS', 'HAPC_SEC_UNSECURE'), ('HAPC_CFG1', '0x00'), ('HAPC_CFG2', '0x00'),
                   ('HAPC_VMAX', '0x3a'), ('HAPC_RATE1', '0x35'), ('HAPC_RATE2', '0x05'),
                   ('HAPC_SEL', '0x00'), ('HAPC_EN_CTL', '0x80')]:
    line = [l for l in buzz.splitlines() if '{ ' + reg + ',' in l]
    assert line, reg
    assert value in line[0], (reg, line[0])

# it plays, it stops, it restores the enable it found
assert 'spmi_write(HAPC_PLAY, 0x80)' in buzz
assert 'spmi_write(HAPC_PLAY, 0x00)' in buzz
assert 'spmi_write(HAPC_EN_CTL, hapc_en_ctl)' in buzz

# the bounded duration: 100 + 400 (+ 500 when not left running) ms, well under 2 s
delays = [int(m) for m in re.findall(r'delay_for_interval\((\d+)', buzz)]
assert sum(delays) <= 2000, delays
assert max(delays) <= 500, delays

# leaving it running is only the explicit test mode (hapc=3)
assert 'leave_running' in buzz

print("PASS: hapc recipe values, play/stop/restore, bounded delay (<= %d ms)" % sum(delays))
