#!/bin/bash
set -uo pipefail
cd /mnt/s/opencode/hakios || exit 1
echo "--- symbols ---"
python3 tools/boot-args.py --symbols > configs/symbols.tsv
echo "rows: $(wc -l < configs/symbols.tsv)"
echo "--- lst ---"
python3 tools/boot-args.py --lst > configs/boot-args.lst
echo "rows: $(wc -l < configs/boot-args.lst)"
echo "--- markdown ---"
python3 tools/boot-args.py --markdown > docs/boot-args.md
echo "lines: $(wc -l < docs/boot-args.md)"
echo "--- check ---"
python3 tools/boot-args.py --check
echo "check rc=$?"
echo "--- docs-gen ---"
python3 tools/docs-gen.py
echo "docs-gen rc=$?"
python3 tools/docs-gen.py --check
echo "docs-gen check rc=$?"
