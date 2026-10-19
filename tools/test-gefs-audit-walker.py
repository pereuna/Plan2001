#!/usr/bin/env python3
"""Exercise the shared C ownership walker with adversarial graphs."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent

@unittest.skipUnless(shutil.which('cc'), 'native C compiler required')
class Walker(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.bin = Path(cls.tmp.name) / 'walker'
        subprocess.run(['cc', '-std=gnu11', '-O0', '-g', '-o', str(cls.bin),
                        str(ROOT / 'tools/test-gefs-audit.c')], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_case(self, name):
        return subprocess.run([self.bin, name], capture_output=True, text=True)

    def test_shared_old_subtrees_and_all_owners(self):
        r = self.run_case('valid')
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertIn('total=60 free=52 owned=8 unaccounted=0 physicalonly=0', r.stdout)
        self.assertIn('audit complete: yes', r.stdout)

    def test_nonzero_count_is_not_structural_failure(self):
        r = self.run_case('unaccounted')
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertIn('total=60 free=51 owned=8 unaccounted=1', r.stdout)

    def test_live_is_not_complete(self):
        r = self.run_case('live')
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertIn('complete=0', r.stdout)
        self.assertNotIn('audit complete: yes', r.stdout)

    def test_invalid_graphs_fail(self):
        for name, error in [('invalid', 'invalid address'), ('cycle', 'tree cycle'),
                            ('chain-cycle', 'chain cycle'), ('free-owned', 'free block owned'),
                            ('duplicate-free', 'duplicate free'),
                            ('conflicting', 'conflicting tree pointer'), ('overflow', 'invalid free range')]:
            with self.subTest(name=name):
                r = self.run_case(name)
                self.assertNotEqual(r.returncode, 0)
                self.assertIn(error, r.stdout)
                self.assertIn('audit complete: no', r.stdout)
                self.assertNotIn('audit complete: yes', r.stdout)

if __name__ == '__main__':
    unittest.main()
