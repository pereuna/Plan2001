#!/usr/bin/env python3
"""Regression checks for test-gefs.sh's result handling, without QEMU.

PLAN9 points to plan9port for the rc check/sync failure tests.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


SOURCE = Path(__file__).with_name('test-gefs.sh').read_text()
CHECK = re.search(r'(?ms)^checkfs\(\) \{\n.*?^\}', SOURCE).group()
DIAGNOSTIC = ('unaccounted blocks: 0 (0 KiB)\n'
              'reachability check: diagnostic only (not validated)\n')
RC = (str(Path(os.environ['PLAN9']) / 'bin/rc') if os.environ.get('PLAN9')
      else shutil.which('rc'))


class Results(unittest.TestCase):
    def check_result(self, output, expected, status=0):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            (p / 'fixture').write_text(output)
            # Exercise the real helper with a transport that can fail even
            # after delivering complete output (timeout or remote error).
            script = '''
vm=$1
fnstatus=$2
run() { cat "$vm/fixture" >"$vm/out"; return "$fnstatus"; }
''' + CHECK + '\ncheckfs fixture\n'
            r = subprocess.run(['bash', '-c', script, 'test', d, str(status)],
                               capture_output=True, text=True)
            self.assertEqual(r.returncode == 0, expected, r.stderr)
            self.assertEqual((p / 'check-fixture.log').read_text(), output)

    def test_structural_success_with_unaccounted_blocks(self):
        for n in (0, 12800):
            with self.subTest(blocks=n):
                self.check_result(f'unaccounted blocks: {n} ({n * 16} KiB)\n'
                                  'reachability check: diagnostic only (not validated)\n'
                                  'structural check: ok\nGEFS-CHECKED\n', True)

    def test_incomplete_or_failed_results(self):
        for output in (
            'GEFS-CHECKED\n',
            'ok\nGEFS-CHECKED\n',
            'structural check: not ok\nGEFS-CHECKED\n',
            'structural check: ok\nGEFS-CHECKED\n',
            'reachability check: unavailable\nstructural check: ok\nGEFS-CHECKED\n',
            DIAGNOSTIC + 'structural check: ok\n',
            'reachability check: diagnostic only (not validated)\n'
            'structural check: ok\nGEFS-CHECKED\n',
            DIAGNOSTIC + 'structural check: not ok\nstructural check: ok\nGEFS-CHECKED\n',
        ):
            with self.subTest(output=output):
                self.check_result(output, False)

    def test_ktest_requires_completed_nonempty_success(self):
        pattern = re.search(r"grep -Eq '([^']*KTEST-DONE[^']*)'", SOURCE).group(1)
        for line, expected in (
            ('KTEST-DONE ok 31 fail 0', True),
            ('KTEST-DONE ok 0 fail 0', False),
            ('KTEST-DONE ok 30 fail 1', False),
            ('KTEST-DONE', False),
            ('KTEST fsobj OK 300ms', False),
        ):
            with self.subTest(line=line):
                r = subprocess.run(['grep', '-Eq', pattern], input=line + '\n',
                                   capture_output=True, text=True)
                self.assertEqual(r.returncode == 0, expected)

    def test_transport_failure_with_complete_output(self):
        self.check_result(DIAGNOSTIC + 'structural check: ok\nGEFS-CHECKED\n',
                          False, status=124)


@unittest.skipUnless(RC, 'rc required (PLAN9=path/to/plan9port)')
class RcFailures(unittest.TestCase):
    def test_ctl_write_failure_stops_completion_marker(self):
        commands = [
            re.search(r'run "(echo check >[^"\n]+)" 600', CHECK).group(1),
            re.search(r'run "(rm -f /tmp/crash\*[^"\n]+)" 300', SOURCE).group(1),
        ]
        for command in commands:
            for mode in ('ok', 'open-error', 'write-error'):
                with self.subTest(command=command, mode=mode):
                    with tempfile.TemporaryDirectory() as d:
                        # Distinguish open failure from a failed write to
                        # an already opened ctl, as a server's Rerror does.
                        broken = mode != 'ok'
                        if mode == 'write-error' and not Path('/dev/full').exists():
                            self.skipTest('/dev/full required for write errors')
                        ctl = {'ok': d + '/ctl', 'open-error': d + '/missing/ctl',
                               'write-error': '/dev/full'}[mode]
                        command_run = command.replace('/n/adm/ctl', ctl)
                        command_run = command_run.replace('/tmp/crash*', d + '/crash*')
                        command_run = command_run.replace('/usr/glenda/', d + '/')
                        r = subprocess.run([RC, '-c', 'r=1; ' + command_run],
                                           capture_output=True, text=True)
                        self.assertEqual(r.returncode == 0, not broken, r.stderr)
                        marker = 'GEFS-CHECKED' if 'echo check' in command else 'synced'
                        self.assertEqual(marker in r.stdout.splitlines(), not broken)


if __name__ == '__main__':
    unittest.main()
