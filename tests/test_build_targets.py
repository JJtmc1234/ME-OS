"""Exercise the actual Make recipes with isolated build commands and test binaries."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MAKEFILE = (ROOT / 'Makefile').read_text()


def recipe(target):
    lines = MAKEFILE.splitlines()
    start = next(i for i, line in enumerate(lines) if line.startswith(target + ':'))
    result = []
    for line in lines[start + 1:]:
        if line.startswith('\t'):
            result.append(line)
        elif result:
            break
    return '\n'.join(result) + '\n'


class BuildTargets(unittest.TestCase):
    def test_failed_second_build_cleans_all_temporary_iso_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'tmp').mkdir()
            (root / 'Makefile').write_text('BUILD=build\nISO=build/me-os.iso\ncheck-reproducible:\n' + recipe('check-reproducible'))
            (root / 'fake-make.sh').write_text('''#!/bin/sh
set -eu
if [ "$1" = clean ]; then rm -rf build; exit 0; fi
if [ -e built-once ]; then exit 42; fi
mkdir -p build
printf first > build/me-os.iso
touch built-once
''')
            result = subprocess.run(['make', 'check-reproducible', 'MAKE=sh ./fake-make.sh'], cwd=root,
                                    env={**os.environ, 'TMPDIR': str(root / 'tmp')}, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue((root / 'built-once').exists(), result.stderr)
            self.assertFalse((root / 'first-iso-check.tmp').exists(), 'failed build leaked the first ISO')
            self.assertEqual(list((root / 'tmp').iterdir()), [], 'failed build leaked its temporary directory')

    def test_every_prerequisite_runs_and_failure_stops_the_suite(self):
        for first_exit, expected in [(0, 'first\nsecond\n'), (7, 'first\n')]:
            with self.subTest(first_exit=first_exit), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                (root / 'build').mkdir()
                for name, code in [('first', first_exit), ('second', 0)]:
                    binary = root / 'build' / name
                    binary.write_text(f'#!/bin/sh\necho {name} >> ran\nexit {code}\n')
                    binary.chmod(0o755)
                (root / 'Makefile').write_text('BUILD=build\ntest-unit: build/first build/second\n' + recipe('test-unit'))
                result = subprocess.run(['make', 'test-unit'], cwd=root, capture_output=True, text=True)
                self.assertEqual(result.returncode == 0, first_exit == 0, result.stderr)
                self.assertTrue((root / 'ran').exists(), 'prerequisite tests were never run')
                self.assertEqual((root / 'ran').read_text(), expected)


if __name__ == '__main__':
    unittest.main()
