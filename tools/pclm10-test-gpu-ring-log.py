#!/usr/bin/env python3
"""Run the actual ring-send path with mocked ring/MMIO/cache/log access."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
text = (root / 'drivers/gpu/hakios.adreno.c').read_text()
start = text.index('static void\nring_send(void)')
end = text.index('\n}\n', start) + 3
function = text[start:end]
prefix = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#define RING_WORDS 64u
#define CP_RB_WPTR 0x10u
#define CP_RB_RPTR 0x14u
static struct {uintptr_t memory;} core_ring = {0x1000};
static struct {uintptr_t base;} gpu = {0x2000};
static uint32_t ring_at, ring_sent, step, gpu_log;
static bool quiet;
static unsigned reads, writes, flushes, lines;
static void to_memory(uintptr_t at, uint32_t size) {
    assert(at >= 0x1000 && at + size <= 0x1100); flushes++;
}
static void wr(uintptr_t at, uint32_t word) {
    assert(at == gpu.base + CP_RB_WPTR && word == ring_at); writes++;
}
static uint32_t rd(uintptr_t at) {
    assert(at == gpu.base + CP_RB_WPTR || at == gpu.base + CP_RB_RPTR);
    reads++; return ring_at;
}
static int log_line(const char *format, ...) { (void)format; lines++; return 0; }
#define printf log_line
'''
suffix = r'''
int main(void) {
    for (unsigned q=0; q<2; q++) for (unsigned diagnostic=0; diagnostic<2; diagnostic++)
        for (unsigned wrap=0; wrap<2; wrap++) {
            quiet=q; gpu_log=diagnostic; ring_sent=wrap?60:2; ring_at=wrap?4:12;
            reads=writes=flushes=lines=0;
            ring_send();
            assert(writes==1 && ring_sent==ring_at && flushes==(wrap?2u:1u));
            assert(lines==(!quiet || gpu_log ? 1u:0u));
            assert(reads==2*lines);
        }
    puts("PASS: 8 ring-send cases; quiet runtime avoids logging/readback, publish and wrap flush unchanged");
}
'''
with tempfile.TemporaryDirectory(prefix='gpu-ring-log-') as directory:
    path = Path(directory)
    (path/'test.c').write_text(prefix + function + suffix)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
