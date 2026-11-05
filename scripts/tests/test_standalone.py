"""Exercise the standalone build interface without network or a Java installation."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SHA = 'f735c12192bf95684e6ae1ae27c400b8170fc6d8'

class StandaloneBuildTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / 'scripts').mkdir()
        (self.root / '.local-build').mkdir()
        (self.root / '.local-build/bob-1.13.0.jar').touch()
        for name in ('config.sh', 'build_demo.sh', 'ensure_bob.sh'):
            shutil.copy2(ROOT / 'scripts' / name, self.root / 'scripts' / name)
        self.java = self.root / 'java'
        self.java.write_text('''#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
if '-version' in sys.argv: print('openjdk version "25"')
elif '--version' in sys.argv: print('bob.jar version: 1.13.0 sha1: ''' + SHA + '''')
else:
    Path(os.environ['CAPTURE']).write_text(json.dumps(sys.argv[1:]))
    sys.exit(int(os.environ.get('BUILD_EXIT', '0')))
''')
        self.java.chmod(0o755)
        self.capture = self.root / 'args.json'
        self.env = {**os.environ, 'JAVA_BIN': str(self.java), 'CAPTURE': str(self.capture),
                    'LOCAL_BUILD_ROOT': str(self.root / '.local-build')}
        for key in ('BUILD_SERVER', 'PLATFORM'):
            self.env.pop(key, None)
    def run_build(self, *args):
        return subprocess.run([str(self.root / 'scripts/build_demo.sh'), *args], env=self.env,
                              capture_output=True, text=True)
    def test_runtime_checker_uses_selected_bundle_resources(self):
        macos = self.root / 'Demo.app/Contents/MacOS'
        resources = self.root / 'Demo.app/Contents/Resources'
        macos.mkdir(parents=True)
        resources.mkdir(parents=True)
        (resources / 'game.projectc').touch()
        unrelated = self.root / 'unrelated-project'
        unrelated.mkdir()
        (unrelated / 'game.projectc').touch()
        engine = macos / 'demo'
        engine.write_text('''#!/usr/bin/env python3
from pathlib import Path
import sys
expected = Path(__file__).resolve().parent.parent / 'Resources/game.projectc'
assert Path(sys.argv[-1]) == expected
assert Path.cwd() == expected.parent
print('GALLERY_RESTART_CACHE_PASSED' if '--config=gallery.verify_cache=1' in sys.argv else 'GALLERY_SMOKE_PASSED')
''')
        engine.chmod(0o755)
        result = subprocess.run(['python3', str(ROOT / 'scripts/smoke_demo.py'), str(engine)],
                                cwd=unrelated, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.count('PASSED'), 3)

    def test_default_build_is_self_contained_and_hosted(self):
        result = self.run_build()
        self.assertEqual(result.returncode, 0, result.stderr)
        args = json.loads(self.capture.read_text())
        self.assertEqual(args[args.index('--root')+1], str(self.root))
        self.assertEqual(args[args.index('--build-server')+1], 'https://build.defold.com')
        self.assertEqual(args[-2:], ['resolve', 'build'])
        self.assertNotIn('--settings', args)
    def test_bundle_and_local_server_override(self):
        self.env.update(BUILD_SERVER='http://127.0.0.1:9010', PLATFORM='x86_64-linux')
        result = self.run_build('bundle')
        self.assertEqual(result.returncode, 0, result.stderr)
        args = json.loads(self.capture.read_text())
        self.assertEqual(args[-3:], ['resolve', 'build', 'bundle'])
        self.assertEqual(args[args.index('--platform')+1], 'x86_64-linux')
        self.assertEqual(args[args.index('--build-server')+1], 'http://127.0.0.1:9010')
    def test_builder_failure_propagates(self):
        self.env['BUILD_EXIT'] = '7'
        self.assertEqual(self.run_build().returncode, 7)
    def test_invalid_command_does_not_build(self):
        self.assertEqual(self.run_build('invalid').returncode, 2)
        self.assertFalse(self.capture.exists())

if __name__ == '__main__':
    unittest.main()
