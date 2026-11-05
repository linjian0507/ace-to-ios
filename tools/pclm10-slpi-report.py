#!/usr/bin/env python3
"""Summarize SLPI experiment evidence from a complete run log, without hardware IO."""

import argparse
from pathlib import Path
import re


def summarize(path):
    lines = path.read_text(errors='replace').splitlines()
    temperatures = []
    counts = []
    flags = []
    cursors = []
    summaries = []
    stops = []
    pattern = re.compile(
        r"dispatch has taken spi (\d+) (\d+) times and spi (\d+) (\d+) times; "
        r"our own handler was entered (\d+) and (\d+) times"
    )
    for number, line in enumerate(lines, 1):
        match = re.search(r'hottest \S+ at ([\d.]+) C', line)
        if match:
            temperatures.append((float(match[1]), number))
        match = pattern.search(line)
        if match:
            values = tuple(map(int, match.groups()))
            counts.append((number, values))
        marker = re.search(r'slpi: m(?:18|19|20|21):', line)
        if marker:
            flags.append((number, line[marker.start():]))
        if 'the descriptor at smem+' in line and 'our queue read to' in line:
            cursors.append((number, line[line.index('slpi: m4:'):]))
        if re.search(r'slpi: m4: after \d+ s:', line):
            summaries.append((number, line[line.index('slpi: m4:'):]))
        # The normal "stop at 60 C" startup line is a threshold, not a stop.
        if 'thermal:' in line and (
            re.search(r'\b(stopping|too hot|limit reached)\b', line, re.I)
            or ('thermal_stop=' in line and 'back to the loader' in line)
        ):
            stops.append((number, line))

    print(f'Log: {path} ({len(lines)} lines)')
    print('Return marker:', 'present' if any('kernel returned:' in line for line in lines) else 'absent')
    if temperatures:
        peak = max(value for value, _ in temperatures)
        number = next(number for value, number in temperatures if value == peak)
        print(f'Full-log temperature peak: {peak:.1f} C [line {number}]')
    else:
        print('Full-log temperature peak: unavailable')
    print(f'Thermal-stop text candidates: {len(stops)} (review log to confirm)')
    seen = set()
    for number, values in counts:
        if values in seen:
            continue
        seen.add(values)
        first_id, first_count, second_id, second_count, first_handler, second_handler = values
        print(f'[line {number}] INTID {first_id}: dispatch {first_count}, handler {first_handler}; '
              f'INTID {second_id}: dispatch {second_count}, handler {second_handler}')
        if (first_count, second_count) != (first_handler, second_handler):
            print('WARNING: dispatcher and handler counts differ; inspect timing and log completeness')
    for evidence in (flags, summaries, cursors, stops):
        seen = set()
        for number, text in evidence:
            if text in seen:
                continue
            seen.add(text)
            print(f'[line {number}] {text}')
    if not counts:
        print('No complete interrupt count records; do not infer zero from missing/truncated lines.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    summarize(parser.parse_args().log)
