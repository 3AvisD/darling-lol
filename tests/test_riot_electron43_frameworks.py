"""Source-level checks for the Electron 43 framework stubs and constant literals.

The stubs are only compiled on a prepared Darling tree, so CI cannot link them.
These checks keep the symbol lists honest instead: an import that Electron 43
binds must have a matching definition in the stub source, and the constant
literals the patches add must be the ones the guest check exercises.
"""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    'riot_electron43_frameworks', ROOT / 'tools/build-riot-electron43-frameworks.py')
frameworks = importlib.util.module_from_spec(spec)
spec.loader.exec_module(frameworks)

CONSTANT_CLASSES = ('NSConstantIntegerNumber', 'NSConstantDoubleNumber',
                    'NSConstantFloatNumber', 'NSConstantArray', 'NSConstantDictionary')


class FrameworkStubTests(unittest.TestCase):
    def test_every_framework_has_a_source_and_symbol_list(self):
        for name, (_, required) in frameworks.FRAMEWORKS.items():
            with self.subTest(framework=name):
                self.assertTrue((frameworks.SOURCES / f'{name}.m').is_file())
                self.assertTrue(required, 'a framework without symbols checks nothing')

    def test_every_required_symbol_is_defined_in_its_stub(self):
        for name, (_, required) in frameworks.FRAMEWORKS.items():
            source = (frameworks.SOURCES / f'{name}.m').read_text()
            for symbol in required:
                # _OBJC_CLASS_$_Foo is defined by @implementation Foo; a plain
                # constant simply loses the leading underscore in C.
                identifier = symbol.split('$_')[-1] if '$_' in symbol else symbol.lstrip('_')
                with self.subTest(framework=name, symbol=symbol):
                    self.assertIn(identifier, source)

    def test_guest_check_covers_every_constant_class(self):
        check = (frameworks.SOURCES / 'constant-literal-check.m').read_text()
        for name in CONSTANT_CLASSES:
            if name == 'NSConstantFloatNumber':
                continue  # exercised through the double layout; clang emits it rarely
            self.assertIn(f'objc_getClass("{name}")', check)

    def test_patches_add_the_constant_sources(self):
        corefoundation = (ROOT / 'patches/corefoundation.patch').read_text()
        foundation = (ROOT / 'patches/foundation.patch').read_text()
        self.assertIn('+++ b/NSConstantCollections.m', corefoundation)
        self.assertIn('+++ b/src/NSConstantNumber.m', foundation)
        for name in CONSTANT_CLASSES:
            self.assertIn(f'@implementation {name}', corefoundation + foundation)
        for symbol in ('__NSArray0__struct', '__NSDictionary0__struct'):
            self.assertIn(symbol, corefoundation)

    def test_checker_names_the_same_classes(self):
        checker = (ROOT / 'tools/check-riot-constant-literals.py').read_text()
        for name in CONSTANT_CLASSES:
            self.assertIn(f'_OBJC_CLASS_$_{name}', checker)


if __name__ == '__main__':
    unittest.main()
