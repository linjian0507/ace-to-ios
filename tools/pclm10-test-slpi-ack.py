#!/usr/bin/env python3
"""Host-only regression checks for PCLM10 SLPI restart ACK and GIC numbering.

Runs the driver's actual acknowledgement function against bounded fake SMEM.
Requires Python 3 and a C11 compiler named cc; no phone or hardware accesses.
"""

from pathlib import Path
import subprocess
import tempfile
import re

root = Path(__file__).resolve().parents[1]
source = (root / 'devices/pclm10/drivers/wifi/hakios.slpi.c').read_text()
start = source.index('static bool\nslpi_smp2p_ack_restart(void)')
end = source.index('\n}\n', start) + 3
function = source[start:end]
header_start = source.index('struct smp2p_header {')
header_end = source.index('\n};', header_start) + 3
header = source[header_start:header_end]
prefix = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define ITEM_SLPI_SMP2P 481
#define ITEM_SLPI_SMP2P_OUT 430
#define SMEM_US 0
#define SMEM_SLPI 3
#define SMP2P_MAGIC 0x504d5324u
#define SMP2P_FLAGS_RESTART_DONE 0
#define SMP2P_FLAGS_RESTART_ACK 1
struct smp2p_header {
    uint32_t magic;
    uint8_t version, features[3];
    uint16_t local_pid, remote_pid, total_entries, valid_entries;
    uint32_t flags;
};
static unsigned char memory[256];
static unsigned kicks, writes;
static bool missing, short_header, fail_readback, fail_write;
static uint32_t smem_item(uint32_t id, uint32_t *size) {
    *size = short_header ? 4 : sizeof(struct smp2p_header);
    return missing ? 0 : id == 481 ? 32 : 128;
}
static bool smem_peek(uint64_t at, void *to, uint32_t size) {
    if (fail_readback && at == 144 && size == 4) return false;
    assert(at + size <= sizeof(memory));
    memcpy(to, memory + at, size);
    return true;
}
static bool smem_poke(uint64_t at, const void *from, uint32_t size) {
    assert(at == 144 && size == 4);
    if (fail_write) return false;
    writes++;
    memcpy(memory + at, from, size);
    return true;
}
static void settle(void) {}
static void slpi_smp2p_kick(const char *why) { (void)why; kicks++; }
static void reset(uint32_t in_flags, uint32_t out_flags) {
    memset(memory, 0xa5, sizeof(memory));
    struct smp2p_header in = {.magic=SMP2P_MAGIC, .version=1,
        .features={1,0,0}, .local_pid=3, .remote_pid=0, .flags=in_flags};
    struct smp2p_header out = {.magic=SMP2P_MAGIC, .version=1,
        .features={1,0,0}, .local_pid=0, .remote_pid=3, .flags=out_flags};
    memcpy(memory + 32, &in, sizeof(in));
    memcpy(memory + 128, &out, sizeof(out));
    kicks = writes = 0;
    missing = short_header = fail_readback = fail_write = false;
}
'''
suffix = r'''
int main(void) {
    _Static_assert(sizeof(struct smp2p_header) == 20, "wire header size");
    _Static_assert(offsetof(struct smp2p_header, flags) == 16, "wire flags offset");
    unsigned char before[256];
    uint32_t flags;
    reset(1, 0x81);
    memcpy(before, memory, sizeof(memory));
    assert(slpi_smp2p_ack_restart());
    assert(kicks == 1 && writes == 1);
    memcpy(&flags, memory + 144, 4);
    assert(flags == 0x83);
    memcpy(before + 144, &flags, 4);
    assert(memcmp(before, memory, sizeof(memory)) == 0);
    assert(!slpi_smp2p_ack_restart());
    assert(kicks == 1 && writes == 1);
    reset(0, 0x83);
    assert(slpi_smp2p_ack_restart());
    memcpy(&flags, memory + 144, 4);
    assert(flags == 0x81 && kicks == 1 && writes == 1);
    reset(0, 0);
    assert(!slpi_smp2p_ack_restart() && !writes && !kicks);
    reset(1, 0); missing = true;
    assert(!slpi_smp2p_ack_restart() && !writes && !kicks);
    reset(1, 0); short_header = true;
    assert(!slpi_smp2p_ack_restart() && !writes && !kicks);
    reset(1, 0); memory[32] = 0;
    assert(!slpi_smp2p_ack_restart() && !writes && !kicks);
    reset(1, 0); memory[37] = 0;
    assert(!slpi_smp2p_ack_restart() && !writes && !kicks);
    reset(1, 0); memory[40] = 2;
    assert(!slpi_smp2p_ack_restart() && !writes && !kicks);
    reset(1, 0); memory[36] = 2;
    assert(!slpi_smp2p_ack_restart() && !writes && !kicks);
    reset(1, 0); fail_readback = true;
    assert(!slpi_smp2p_ack_restart() && writes == 1 && !kicks);
    reset(1, 0); fail_write = true;
    memcpy(before, memory, sizeof(memory));
    assert(!slpi_smp2p_ack_restart() && !writes && !kicks);
    assert(memcmp(before, memory, sizeof(memory)) == 0);
    puts("PASS: 12 restart-ack cases; only flags touched; invalid/write/readback failures never ring");
}
'''
# Use the actual driver's wire header too, so a layout regression is not
# hidden by a separately maintained test-only definition.
test_header_start = prefix.index('struct smp2p_header {')
test_header_end = prefix.index('\n};', test_header_start) + 3
prefix = prefix[:test_header_start] + header + prefix[test_header_end:]
with tempfile.TemporaryDirectory(prefix='m18-ack-') as directory:
    path = Path(directory)
    (path / 'test.c').write_text(prefix + function + suffix)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)

expected = {'SPI_GLINK_RX': (202, 170), 'SPI_SMP2P_RX': (204, 172)}
for name, (corrected, historical) in expected.items():
    match = re.search(r'^#define ' + name + r'\s+\(slpi_level >= 17 \? (\d+)u : (\d+)u\)$', source, re.M)
    assert match and tuple(map(int, match.groups())) == (corrected, historical), name
for intid in (202, 204):
    assert intid // 32 == 6
    assert 0xc00 + 4 * (intid // 16) == 0xc30
assert (1 << (202 % 32)) | (1 << (204 % 32)) == 0x1400
assert (2 << (2 * (202 % 16))) | (2 << (2 * (204 % 16))) == 0x02200000
print('PASS: corrected interrupt word, mask and edge encoding')
