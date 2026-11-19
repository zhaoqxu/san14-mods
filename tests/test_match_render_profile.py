from pathlib import Path
import importlib.util,json,sys,tempfile,unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import match_render_profile as profile

class Config:
    def __init__(self):self.values={'WIDTH':3840,'HEIGHT':2160,'WINDOW_MODE':1};self.writes=0;self.fail=False
    def read(self):return dict(self.values)
    def write(self,values):
        self.writes+=1;self.values.update(values)
        if self.fail:self.fail=False;raise OSError('registry write failed')

class RenderProfileTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.backup=Path(self.temp.name)/'render.json';self.config=Config()
        self.patcher=patch.object(profile,'processes_running',return_value=False);self.patcher.start();self.addCleanup(self.patcher.stop)
    def test_restore_round_trip_and_idempotency(self):
        before=self.config.read();profile.switch('apply',self.backup,self.config);self.assertEqual(self.config.read(),profile.LOW)
        profile.switch('restore',self.backup,self.config);self.assertEqual(self.config.read(),before)
        profile.switch('restore',self.backup,self.config);self.assertEqual(self.config.writes,2)
    def test_running_game_does_not_create_backup_or_write(self):
        with patch.object(profile,'processes_running',return_value=True):
            with self.assertRaisesRegex(RuntimeError,'Close SAN14'):profile.switch('apply',self.backup,self.config)
        self.assertFalse(self.backup.exists());self.assertEqual(self.config.writes,0)
    def test_launch_race_preserves_durable_backup_without_writing(self):
        with patch.object(profile,'processes_running',side_effect=[False,True]):
            with self.assertRaisesRegex(RuntimeError,'started during preparation'):profile.switch('apply',self.backup,self.config)
        self.assertEqual(self.config.writes,0);self.assertEqual(json.loads(self.backup.read_text())['state'],'prepared')
    def test_existing_backup_is_never_overwritten_on_apply(self):
        profile.switch('apply',self.backup,self.config);before=self.backup.read_bytes()
        with self.assertRaises(FileExistsError):profile.switch('apply',self.backup,self.config)
        self.assertEqual(self.backup.read_bytes(),before);self.assertEqual(self.config.writes,1)
    def test_changed_settings_refuse_restore(self):
        profile.switch('apply',self.backup,self.config);self.config.values['WIDTH']=1920
        with self.assertRaisesRegex(RuntimeError,'changed since apply'):profile.switch('restore',self.backup,self.config)
        self.assertEqual(self.config.values['WIDTH'],1920);self.assertEqual(self.config.writes,1)
    def test_failed_registry_write_rolls_back(self):
        before=self.config.read();self.config.fail=True
        with self.assertRaises(OSError):profile.switch('apply',self.backup,self.config)
        self.assertEqual(self.config.read(),before);self.assertEqual(json.loads(self.backup.read_text())['state'],'prepared')
        profile.switch('restore',self.backup,self.config);self.assertEqual(self.config.writes,2)
    def test_changed_registry_target_refused(self):
        profile.switch('apply',self.backup,self.config);receipt=json.loads(self.backup.read_text());receipt['key']='Software/Other';self.backup.write_text(json.dumps(receipt))
        with self.assertRaises(ValueError):profile.switch('restore',self.backup,self.config)
        self.assertEqual(self.config.writes,1)

if __name__=='__main__':unittest.main()
