from hashlib import sha256
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from zipfile import ZipFile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'manager/tools'))
spec=importlib.util.spec_from_file_location('runtime_package',ROOT/'manager/tools/package_runtime.py')
runtime=importlib.util.module_from_spec(spec);spec.loader.exec_module(runtime)


class RuntimePackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory();self.addCleanup(self.temporary.cleanup)
        self.root=Path(self.temporary.name);self.adapter=self.root/'adapter'
        base=self.adapter/'SAN14Base/GameAdapter';base.mkdir(parents=True)
        binary=b'MZ-fixture';(base/'GameAdapter.dll').write_bytes(binary)
        manifest={'schema_version':1,'abi_version':1,'sdk_abi_version':65536,'binary':'GameAdapter.dll',
            'embedded_commands':True,'adapter_id':'fixture','adapter_version':'0.1.0','sha256':sha256(binary).hexdigest()}
        (base/'adapter.json').write_text(json.dumps(manifest))
        for name in ('README.txt','THIRD-PARTY-NOTICES.txt','licenses/library/LICENSE'):
            path=self.adapter/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(name)
        self.core=self.root/'core.dll';self.core.write_bytes(b'core')
        self.output=self.root/'runtime.zip'

    def test_runtime_retains_adapter_notices_and_licenses(self):
        runtime.package(self.core,self.adapter,self.output)
        with ZipFile(self.output) as archive:
            self.assertEqual(archive.read('SAN14Base/GameAdapter/licenses/library/LICENSE'),b'licenses/library/LICENSE')
            self.assertIn('SAN14Base/GameAdapter/THIRD-PARTY-NOTICES.txt',archive.namelist())
            self.assertIn('dinput8.dll',archive.namelist())
            self.assertEqual(archive.read('SAN14Base/tools/run_match.py'),(ROOT/'tools/run_match.py').read_bytes())
            self.assertEqual(archive.read('SAN14Base/tools/MATCH_RUNNER.md'),(ROOT/'sdk/docs/MATCH_RUNNER.md').read_bytes())
            self.assertEqual(archive.read('SAN14Base/tools/match_render_profile.py'),(ROOT/'tools/match_render_profile.py').read_bytes())
            self.assertNotIn('SAN14Base/tools/generate_match_profile.py',archive.namelist())

    def test_missing_adapter_notice_blocks_distribution(self):
        (self.adapter/'THIRD-PARTY-NOTICES.txt').unlink()
        with self.assertRaisesRegex(ValueError,'notice missing'):runtime.package(self.core,self.adapter,self.output)
        self.assertFalse(self.output.exists())


if __name__=='__main__':unittest.main()
