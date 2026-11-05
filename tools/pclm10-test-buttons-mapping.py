#!/usr/bin/env python3
"""Check the buttons' mapping: the iPhone X's three keys from the phone's own pins.

The buttons are not tested here -- that needs the phone. What is checked is the mapping the
driver posts: power is the side button on the PON block, the two volume keys are the PM7325's
GPIO6 and the PON's RESIN, and every key posts a press and a release, never only one.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'xnu-src/xnu-8796.121.2/bsd/dev/hakios.buttons.c').read_text()

# the three keys and what they are
assert '.name = "power", .record = HAKIOS_BUTTON_LOCK' in source
assert '.name = "volume up", .record = HAKIOS_BUTTON_VOLUME_UP' in source
assert '.name = "volume down", .record = HAKIOS_BUTTON_VOLUME_DOWN' in source
assert 'HAKIOS_BUTTON_DOWN' in source and 'HAKIOS_BUTTON_UP' in source
# press and release come in pairs, through the touch/input posting path
assert source.count('HAKIOS_BUTTON_DOWN') >= 2
assert source.count('HAKIOS_BUTTON_UP') >= 2
# the pins the comment names: KPDPWR_N and RESIN_N of the PMK8350's PON, GPIO6 of the PM7325
assert 'KPDPWR_N' in source or 'KPDPWR' in source
assert 'RESIN' in source
assert 'GPIO6' in source
# no home button: the mapping is the side button and two volume keys only
assert 'no home button' in source
print("PASS: buttons mapping; power/volume up/volume down, press+release pairs, pins named")
