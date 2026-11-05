#!/usr/bin/env python3
"""Exercise the actual M19 fail-closed gate with mocked secure-call statuses."""

from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'devices/pclm10/drivers/wifi/hakios.slpi.c').read_text()
start = source.index('\t\tif (slpi_level == 19 || slpi_level == 20 || slpi_level == 21) {\n\t\t\tuint64_t answer = ~0ull;')
depth = 0
for end in range(source.index('{', start), len(source)):
    if source[end] == '{':
        depth += 1
    elif source[end] == '}':
        depth -= 1
        if depth == 0:
            break
gate = source[start:end + 1]
rewrite = source.index('if (slpi_smp2p_make_out() != 0)', end)
assert end < rewrite < source.index('started = slpi_start();', end)
assert re.search(r'^#define SCM_PAS_SHUTDOWN\s+SCM_CALL\(2, 6\)$', source, re.M)
prefix = r'''
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#define SCM_PAS_SHUTDOWN 0x206
#define SCM_ONE_VALUE 1
#define PAS_SLPI 12
static unsigned slpi_level = 19, secure_calls, terminated, rewrites, baselines;
static int transport;
static uint64_t firmware;
static int scm(const char *label, uint64_t call, uint64_t nargs, uint64_t client,
               uint64_t unused1, uint64_t unused2, uint64_t *answer) {
    (void)label;
    assert(call == SCM_PAS_SHUTDOWN && nargs == 1 && client == 12);
    assert(unused1 == 0 && unused2 == 0 && rewrites == 0);
    secure_calls++;
    *answer = firmware;
    return transport;
}
static int current_thread(void) { return 7; }
static void thread_terminate(int thread) { assert(thread == 7); terminated++; }
static void slpi_crash_baseline(void) { assert(slpi_level >= 20); baselines++; }
static void attempt(void) {
'''
suffix = r'''
    rewrites++;
}
int main(void) {
    const int statuses[] = {0, -2, 0, -1};
    const uint64_t answers[] = {0, 0, 1, UINT64_MAX};
    for (slpi_level = 19; slpi_level <= 21; slpi_level++) {
        for (unsigned i = 0; i < 4; i++) {
            secure_calls = terminated = rewrites = baselines = 0;
            transport = statuses[i]; firmware = answers[i];
            attempt();
            assert(secure_calls == 1);
            assert(rewrites == (i == 0 ? 1u : 0u));
            assert(terminated == (i == 0 ? 0u : 1u));
            assert(baselines == (i == 0 && slpi_level >= 20 ? 1u : 0u));
        }
    }
    puts("PASS: 12 M19-M21 gate cases; rewrite and crash baseline require both secure-call statuses to succeed");
}
'''
with tempfile.TemporaryDirectory(prefix='m19-shutdown-') as directory:
    path = Path(directory)
    (path / 'test.c').write_text(prefix + gate + suffix)
    # The kernel's printf accepts its uint64_t typedef for %llx; make that
    # conversion explicit only in this portable host harness.
    text = (path / 'test.c').read_text().replace('stopped, answer);', 'stopped, (unsigned long long)answer);')
    (path / 'test.c').write_text(text)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
