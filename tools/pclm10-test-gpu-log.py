#!/usr/bin/env python3
"""Host-only tests: reject software fallback as an interaction prerequisite."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('gpu_log', Path(__file__).with_name('pclm10-check-gpu-log.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class GPULogTests(unittest.TestCase):
    good = '\n'.join([
        'cmdline: debug=0x8 bootcpu=7 adreno=22 adreno_keep=1',
        'hakios-gl: an OpenGL ES 3 context for CoreAnimation: OpenGL ES 3.2; Adreno 640; shaders',
        'HakiosGPU: submission 1: 4 command buffers',
        'MiniIOMobileFramebuffer: swap 1 (#0)',
    ])

    def test_hardware_evidence(self):
        self.assertEqual(module.check(self.good)[0], [])

    def test_software_fallback_even_with_gpu_words(self):
        text = self.good + '\nhakios-gl: context for CoreAnimation: OpenGL ES 3.1; softpipe; shaders'
        self.assertIn('CoreAnimation used a software renderer', module.check(text)[0])

    def test_renderer_name_alone_not_enough(self):
        errors, _ = module.check(self.good.replace('HakiosGPU: submission', 'unrelated: submission'))
        self.assertIn('missing evidence: submission', errors)

    def test_reference_model_is_rejected(self):
        self.assertTrue(module.check(self.good.replace('Adreno 640', 'Adreno 7c+ Gen 3'))[0])

    def test_mesa_fd640_name(self):
        self.assertEqual(module.check(self.good.replace('Adreno 640', 'FD640'))[0], [])

    def test_fault_and_incomplete_frames_are_rejected(self):
        for message in ('adreno: 0: THE SMMU REFUSED the 3D core: address 0x3500',
                        'adreno: gpu: no answer: submission 3495',
                        'MiniIOMobileFramebuffer: the GPU is not done with surface 4 after 500 ms',
                        'adreno: GPU given back (STILL AT WORK after 500000 us)'):
            self.assertIn('GPU fault/stall/incomplete-frame evidence present', module.check(self.good + '\n' + message)[0])

    def test_aggregate_submission_survives_truncated_start(self):
        text = self.good.replace('HakiosGPU: submission 1: 4 command buffers',
                                 'HakiosGPU: 207 submissions in 7388 ms: 1056 command buffers')
        self.assertEqual(module.check(text)[0], [])

    def test_shutdown_fault_not_dated_as_runtime(self):
        fault = 'adreno: 0: THE SMMU REFUSED the 3D core: address 0x1600'
        errors, evidence = module.check(self.good + '\n--- loader_return_after expired ---\n' + fault)
        self.assertTrue(errors)  # still fails the complete run, do not hide teardown faults
        self.assertIn('shutdown_failure', evidence)
        self.assertNotIn('runtime_or_undated_failure', evidence)

    def test_appended_message_buffer_is_history(self):
        text = self.good + '\n--- loader_return_after expired ---\n--- kernel message buffer ---\n'
        text += '[1.0]: MiniIOMobileFramebuffer: the GPU is not done with surface 3 after 500 ms'
        _, evidence = module.check(text)
        self.assertIn('runtime_or_undated_failure', evidence)

    def test_runtime_then_shutdown_both_reported(self):
        text = self.good + '\nadreno: gpu: no answer: submission 3495\n'
        text += 'adreno: the run is about to end: the GPU is given back\n'
        text += 'adreno: 0: THE SMMU REFUSED the 3D core: address 0x1600'
        _, evidence = module.check(text)
        self.assertIn('runtime_or_undated_failure', evidence)
        self.assertIn('shutdown_failure', evidence)

    def test_timeout_line_is_not_successful_swap(self):
        text = self.good.replace('MiniIOMobileFramebuffer: swap 1 (#0)',
                                 'MiniIOMobileFramebuffer: swap 1: the GPU is not done with surface 3 after 500 ms')
        self.assertIn('missing evidence: swap', module.check(text)[0])

    def test_truncated_recipe(self):
        self.assertIn('runtime GPU recipe missing/disabled/truncated',
                      module.check(self.good.replace('adreno_keep=1', ''))[0])

    def test_missing_and_oversized_log_arguments(self):
        self.assertIn('missing evidence: cmdline', module.check('partial log')[0])
        self.assertIn('runtime command line exceeds 607 characters',
                      module.check(self.good.replace('debug=0x8', 'debug=' + 'a' * 610))[0])


if __name__ == '__main__':
    unittest.main()
