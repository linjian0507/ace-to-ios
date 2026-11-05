#!/usr/bin/env python3
"""Fail a PCLM10 GPU acceptance prerequisite on software fallback or missing evidence.

This checks a complete run log; success is NOT a user-interaction acceptance.
"""

import argparse
from pathlib import Path
import re


def check(text):
    lines = text.splitlines()
    evidence = {}
    shutdown = False
    for number, line in enumerate(lines, 1):
        if '--- kernel message buffer ---' in line:
            # Timestamped history is appended after shutdown; it is a replay,
            # not additional shutdown-time activity. Be conservative until
            # another explicit shutdown marker in that history is seen.
            shutdown = False
        if 'the run is about to end: the GPU is given back' in line:
            shutdown = True
        if '--- loader_return_after expired ---' in line:
            shutdown = True
        if 'context for CoreAnimation' in line:
            if re.search(r'\b(softpipe|llvmpipe)\b', line, re.I):
                evidence.setdefault('software', (number, line))
            if re.search(r';\s*(?:Adreno\b|FD640\b)', line):
                evidence.setdefault('adreno', (number, line))
                if not re.search(r';\s*(?:Adreno\s+640|FD640)\b', line):
                    evidence.setdefault('wrong_model', (number, line))
        if re.search(r'HakiosGPU: submission\s+\d+', line):
            evidence.setdefault('submission', (number, line))
        if re.search(r'HakiosGPU: [1-9]\d* submissions in', line):
            evidence.setdefault('submission', (number, line))
        if ('THE SMMU REFUSED the 3D core' in line or 'adreno: gpu: no answer:' in line
                or 'the GPU is not done with surface' in line or 'STILL AT WORK after' in line):
            evidence.setdefault('gpu_failure', (number, line))
            phase = 'shutdown_failure' if shutdown else 'runtime_or_undated_failure'
            evidence.setdefault(phase, (number, line))
        if (re.search(r'MiniIOMobileFramebuffer: swap\s+\d+\s+\(#\d+\)', line)
                or re.search(r'MiniIOMobileFramebuffer: [1-9]\d* frames in', line)):
            evidence.setdefault('swap', (number, line))
        if line.startswith('cmdline: debug='):
            evidence.setdefault('cmdline', (number, line))
    errors = []
    if 'software' in evidence:
        errors.append('CoreAnimation used a software renderer')
    if 'wrong_model' in evidence:
        errors.append('Mesa did not select Adreno 640; reference-board identity is not acceptable')
    if 'gpu_failure' in evidence:
        errors.append('GPU fault/stall/incomplete-frame evidence present')
    for key in ('adreno', 'submission', 'swap', 'cmdline'):
        if key not in evidence:
            errors.append('missing evidence: ' + key)
    if 'cmdline' in evidence:
        args = evidence['cmdline'][1].removeprefix('cmdline: ')
        words = dict(word.split('=', 1) for word in args.split() if '=' in word)
        if words.get('adreno') != '22' or words.get('adreno_keep') != '1':
            errors.append('runtime GPU recipe missing/disabled/truncated')
        if len(args) > 607:
            errors.append('runtime command line exceeds 607 characters')
    return errors, evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    args = parser.parse_args()
    errors, evidence = check(args.log.read_text(errors='replace'))
    for key, (number, line) in evidence.items():
        print(f'{key} [line {number}]: {line}')
    for error in errors:
        print('FAIL: ' + error)
    if not errors:
        print('PASS: GPU prerequisite evidence only; sustained rendering and user acceptance remain unverified')
    return bool(errors)


if __name__ == '__main__':
    raise SystemExit(main())
