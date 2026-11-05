#!/usr/bin/env python3
"""Compile the real GPU GET_PARAM function with the PCLM10 identity constants."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'drivers/gpu/hakios.adreno.c').read_text()
names = ('GPU_CHIP_ID', 'GPU_GMEM_BYTES', 'GPU_HIGHEST_BANK_BIT', 'GPU_UBWC_SWIZZLE', 'GPU_MACROTILE_MODE')
constants = '\n'.join(re.search(r'^#define ' + name + r'\s+[^\n]+', source, re.M)[0] for name in names)
start = source.index('bool\nhakios_gpu_parameter(')
end = source.index('\n}\n', start) + 3
prefix = '#include <stdint.h>\n#include <stdbool.h>\n#include <assert.h>\n#include <stdio.h>\n'
prefix += 'static uint32_t core_khz = 600000;\nstatic uint64_t mach_absolute_time(void) {return 123;}\n'
suffix = r'''
int main(void) {
    const uint32_t keys[] = {2,3,0x10,0x12,0x13};
    const uint64_t values[] = {1048576,0x06040001,15,6,0};
    for (unsigned i=0; i<5; i++) {
        uint64_t value=UINT64_MAX;
        assert(hakios_gpu_parameter(keys[i], &value) && value==values[i]);
    }
    uint64_t value=123;
    assert(!hakios_gpu_parameter(0xffff, &value) && value==123);
    puts("PASS: A640 GET_PARAM identity/GMEM/bank/swizzle/macrotile and unknown-key handling");
}
'''
dts = (root / 'devices/pclm10/vendor-src/dtb/android-merged.dts').read_text()
assert 'qcom,chipid = <0x6040001>;' in dts and 'qcom,highest-bank-bit = <0x0f>;' in dts
with tempfile.TemporaryDirectory(prefix='a640-identity-') as directory:
    path = Path(directory)
    (path/'test.c').write_text(prefix + constants + '\n' + source[start:end] + suffix)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
