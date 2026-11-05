#!/usr/bin/env python3
"""Test the fb_beam SoC gate for real: the probe must not run on the SM8150's display controller.

beamKverHas is the whole decision; the caller only adds the boot string. The cases are the
kernel versions the two phones actually print and the edges of a substring search.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
candidates = [
    ROOT / 'xnu-src/xnu-8796.121.2/iokit/Kernel/MiniIOMobileFramebuffer.cpp',   # the build tree
    ROOT / 'xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp',              # the repository's copy
]
source = None
for p in candidates:
    if p.is_file() and 'beamKverHas' in p.read_text(errors='replace'):
        source = p.read_text()
        break
assert source is not None, ('no MiniIOMobileFramebuffer.cpp with beamKverHas in ' + str(ROOT))
start = source.index('beamKverHas(const char *kver, const char *soc)')
block_start = source.rindex('static bool', 0, start)
end = source.index('\n}', start) + 2
func = source[block_start:end]
assert 'for (const char *p = kver' in func and '*b == 0' in func

harness = r'''
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
''' + func + r'''
int main(void) {
    // the two phones' own strings
    assert(!beamKverHas("2.1.13-hakios-darwin22.5-sc7280-ga3384e03", "-sm8150-"));
    assert(beamKverHas("2.1.13-hakios-darwin22.5-sm8150-ga3384e03", "-sm8150-"));
    // the gate needs the whole token delimited on both sides
    assert(!beamKverHas("sm8150", "-sm8150-"));
    assert(!beamKverHas("-sm8150", "-sm8150-"));
    assert(beamKverHas("x-sm8150-y", "-sm8150-"));
    assert(beamKverHas("a-sm8150-", "-sm8150-"));
    // a substring is a match, an absent one is not
    assert(beamKverHas("-sm8150-", "-sm8150-"));
    assert(!beamKverHas("", "-sm8150-"));
    // an empty needle matches at once (the caller never passes one)
    assert(beamKverHas("anything", ""));
    printf("PASS: 9 beam gate cases; both phones' strings and the substring edges\n");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='beam-gate-test-') as tmp:
    path = Path(tmp)
    (path / 'test.c').write_text(harness)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
