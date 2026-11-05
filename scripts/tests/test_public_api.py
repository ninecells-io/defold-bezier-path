"""Keep the user-facing reference and editor metadata complete."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]

class PublicApiTests(unittest.TestCase):
    def test_wrapper_functions_are_documented(self):
        wrapper = (ROOT / 'bezierpath/bezierpath.lua').read_text()
        readme = (ROOT / 'README.md').read_text()
        for name in re.findall(r'^function M\.(\w+)\(', wrapper, re.M):
            with self.subTest(function=name):
                self.assertRegex(readme, r'\| `'+name+r'\(')
    def test_native_editor_declarations_cover_bindings(self):
        source = (ROOT / 'bezierpath/src/bezierpath.cpp').read_text()
        binding_section = source.split('Module_methods[] = {', 1)[1].split('};', 1)[0]
        names = set(re.findall(r'\{"(\w+)"', binding_section))
        ide = (ROOT / 'bezierpath/idea_autocomplete.lua').read_text()
        self.assertEqual(names, set(re.findall(r'function bezierpath\.(\w+)\(', ide)))
        metadata = (ROOT / 'bezierpath/api/bezierpath.script_api').read_text()
        self.assertEqual(names, set(re.findall(r'^  - name: (\w+)', metadata, re.M)))

if __name__ == '__main__':
    unittest.main()
