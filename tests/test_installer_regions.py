"""Stop at a stub fetcher: verify installer routing without network or runtime use."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class InstallerRegions(unittest.TestCase):
    def test_aliases_and_platform_codes_reach_the_expected_cdn_region(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'scripts').mkdir()
            (root / 'build/runtime/tools').mkdir(parents=True)
            shutil.copy(ROOT / 'scripts/installer.sh', root / 'scripts')
            (root / 'scripts/configure.py').write_text('')
            (root / 'build/runtime/tools/fetch-installer.sh').write_text(
                'printf "%s" "$1" > "$TEST_REGION_LOG"\nexit 23\n')
            log = root / 'region'
            env = dict(os.environ, TEST_REGION_LOG=str(log))
            env.pop('DARLING_WORKSPACE', None)
            for region, expected in [('lan', 'la1'), ('las', 'la2'), ('oce', 'oc1'),
                                     ('la1', 'la1'), ('la2', 'la2'), ('oc1', 'oc1'),
                                     ('euw', 'euw')]:
                with self.subTest(region=region):
                    log.unlink(missing_ok=True)
                    result = subprocess.run(['sh', str(root / 'scripts/installer.sh'), region],
                                            env=env, capture_output=True, text=True, timeout=10)
                    self.assertEqual(result.returncode, 23, result.stderr)
                    self.assertEqual(log.read_text(), expected)
