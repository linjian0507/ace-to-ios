#!/usr/bin/env python3
"""Validate the PCLM10 interactive entry without running or accessing a phone."""

from pathlib import Path
import importlib.util
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class InteractiveTests(unittest.TestCase):
    def test_recovery_hold_blocks_actual_run_before_device_access(self):
        hold = ROOT / 'devices/pclm10/configs/interactive-hold.txt'
        self.assertTrue(hold.is_file())
        result = subprocess.run(['bash', str(ROOT / 'tools/pclm10-interactive.sh')],
                                cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 3)
        self.assertIn('HOLD; no device query, upload or run was attempted', result.stderr)
        self.assertNotIn('effective boot arguments:', result.stdout)

    def test_dry_run_pins_input_and_safety_and_disables_experiments(self):
        result = subprocess.run(['bash', str(ROOT / 'tools/pclm10-interactive.sh'), '--dry-run'],
                                cwd=ROOT, capture_output=True, text=True, check=True)
        line = next(line for line in result.stdout.splitlines() if 'effective boot arguments:' in line)
        words = line.split('effective boot arguments:', 1)[1].split()
        args = dict(word.split('=', 1) for word in words)
        self.assertEqual(len(args), len(words), 'no duplicate boot-argument keys')
        for key, value in {'bootcpu': '7', 'touch': '1', 'pon': '1', 'cpufreq': '0',
                           'thermal': '1', 'thermal_pclm10': '1', 'thermal_stop': '60',
                           'adreno': '22', 'adreno_skip': '0x1cc0', 'adreno_keep': '1',
                           'adreno_no_oob': '1', 'adreno_gpucc_restore': '1',
                           'adreno_vendor_rptr': '1', 'adreno_vendor_nc': '1',
                           'adreno_vendor_protect': '1', 'adreno_halt_observe': '1', 'adreno_walk': '1'}.items():
            self.assertEqual(args.get(key), value, key)
        for key in ('gl', 'slpi', 'wifi', 'wcnbt', 'cam', 'flash', 'hapc', 'aw8697'):
            self.assertNotIn(key, args, key)
        self.assertNotIn('ufs_write', args)
        self.assertNotIn('var_store', args)
        # Full GPU recipe in the successful 014918 log used ~180 bytes of
        # loader/xl prefix; reserve 210, and recheck the actual runtime line.
        self.assertLessEqual(len(' '.join(words)) + 210, 607)

    def test_gpu_recipe_is_pinned_not_software_fallback(self):
        recipe = (ROOT / 'devices/pclm10/configs/interactive-gpu.args').read_text().split()
        self.assertEqual(len(recipe), 10)
        self.assertIn('adreno=22', recipe)
        self.assertIn('adreno_keep=1', recipe)
        script = (ROOT / 'tools/pclm10-interactive.sh').read_text()
        self.assertNotIn('pon=1 gl=1', script)
        self.assertNotIn('adreno=0', script)
        self.assertIn('a640_zap.b02', script)

    def test_refuses_experiment_override(self):
        result = subprocess.run(['bash', str(ROOT / 'tools/pclm10-interactive.sh'), '--dry-run', 'slpi=21'],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)

    def test_source_defaults_have_input_without_enabling_unfinished_probes(self):
        result = subprocess.run(['python3', str(ROOT / 'tools/hakios-config.py'), 'args',
                                 '--defconfig', 'pclm10_defconfig'], cwd=ROOT,
                                capture_output=True, text=True, check=True)
        args = dict(word.split('=', 1) for word in result.stdout.split())
        self.assertEqual(args['touch'], '1')
        self.assertEqual(args['pon'], '1')
        self.assertEqual(args['bootcpu'], '7')
        for key in ('slpi', 'flash', 'aw8697', 'cam', 'wcnbt', 'ufs_write', 'var_store'):
            self.assertNotIn(key, args)

    def test_manifest_does_not_call_test_only_parts_works(self):
        spec = importlib.util.spec_from_file_location('device', ROOT / 'tools/hakios-device.py')
        device = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(device)
        states = {area: state for area, state, _ in device.statuses(device.describe('device', 'pclm10'))}
        for area in ('touch', 'buttons', 'gpu', 'haptic', 'torch', 'camera', 'wifi', 'bluetooth'):
            self.assertEqual(states[area], 'partial', area)

    def test_user_checklist_and_readme_link_exist(self):
        checklist = ROOT / 'devices/pclm10/notes/功能与交付状态.md'
        self.assertTrue(checklist.is_file())
        readme = (ROOT / 'devices/pclm10/README.md').read_text(encoding='utf-8')
        self.assertIn('notes/功能与交付状态.md', readme)
        text = checklist.read_text(encoding='utf-8')
        for boundary in ('真机验证', '默认集成', '交付到手机', '未完成', '不刷机'):
            self.assertIn(boundary, text)

    def test_plan_covers_all_acceptance_items_and_release_gate(self):
        import re
        plan = (ROOT / 'devices/pclm10/notes/功能落实计划.md').read_text(encoding='utf-8')
        features = re.findall(r'^- \[[ x]\] \*\*(F\d\d) ', plan, re.M)
        releases = re.findall(r'^- \[[ x]\] \*\*(R\d\d) ', plan, re.M)
        self.assertEqual(features, [f'F{i:02d}' for i in range(1, 24)])
        self.assertEqual(releases, [f'R{i:02d}' for i in range(1, 7)])
        self.assertIn('条件授权', plan)
        self.assertIn('当前不提前刷写', plan)
        self.assertTrue((ROOT / 'devices/pclm10/AGENTS.md').is_file())

    def test_plan_and_status_relative_links(self):
        import re
        base = ROOT / 'devices/pclm10'
        for path in (base / 'README.md', base / 'notes/功能与交付状态.md', base / 'notes/功能落实计划.md'):
            for link in re.findall(r'\]\(([^)]+)\)', path.read_text(encoding='utf-8')):
                if '://' not in link:
                    self.assertTrue((path.parent / link).exists(), f'{path}: {link}')


if __name__ == '__main__':
    unittest.main()
