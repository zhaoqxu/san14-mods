import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('match_cli',ROOT/'tools/run_match.py')
cli=importlib.util.module_from_spec(spec);spec.loader.exec_module(cli)

class MatchClientTests(unittest.TestCase):
    def endpoint(self,root):
        p=root/'runtime.json';(root/'inbox').mkdir();(root/'outbox').mkdir()
        value={'protocol':1,'pid':123,'creation_filetime':456,'nonce':'instance','available':True}
        cli.atomic_json(p,value);return p,value
    def test_mailbox_process_binding_and_response(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);p,e=self.endpoint(root);captured=[]
            def server():
                deadline=time.monotonic()+3
                while time.monotonic()<deadline:
                    for incoming in (root/'inbox').glob('*.json'):
                        request=json.loads(incoming.read_text());captured.append(request)
                        cli.atomic_json(root/'outbox'/incoming.name,{'ok':True,'result':0,'complete':False});incoming.unlink();return
                    time.sleep(.01)
            worker=threading.Thread(target=server);worker.start()
            with patch.object(cli,'process_creation',return_value=456):
                result=cli.Client(p,e).request('status',handle=42)
            worker.join();self.assertFalse(result['complete']);self.assertEqual(captured[0]['nonce'],'instance');self.assertEqual(captured[0]['handle'],42);self.assertFalse(list((root/'outbox').iterdir()))
    def test_stale_pid_refused_without_submission(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);p,e=self.endpoint(root)
            with patch.object(cli,'process_creation',return_value=999):
                with self.assertRaisesRegex(RuntimeError,'game_closed'):cli.Client(p,e).request('start',options={})
            self.assertFalse(list((root/'inbox').iterdir()))
    def test_timeout_preserves_durable_identity_and_does_not_retry(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);p,e=self.endpoint(root);out=root/'result.json';calls=[]
            def uncertain(self,operation,**fields):
                calls.append((operation,fields));raise TimeoutError('unknown submission')
            with patch.object(cli,'discover',return_value=(p,e)),patch.object(cli.Client,'request',uncertain):
                result=cli.main(['--save','svdexceSC16','--run-id','durable-id','--output',str(out)])
            self.assertEqual(result,2);self.assertEqual(len(calls),1)
            state=json.loads(out.with_suffix('.json.state.json').read_text());self.assertEqual(state['options']['request_id'],'durable-id');self.assertIsNone(state['handle'])
            result=json.loads(out.read_text());self.assertIsNone(result['winner_force_id']);self.assertFalse(result['complete'])
    def test_resume_observes_existing_handle_without_start(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);p,e=self.endpoint(root);state=root/'old.state.json';out=root/'resumed.json'
            cli.atomic_json(state,{'endpoint_path':str(p),'endpoint':e,'handle':42,'options':{'request_id':'same'}});calls=[]
            def response(self,op,**fields):
                calls.append(op);return {'ok':True,'phase':'completed','complete':True,'reason':'date_limit','winner_force_id':None}
            with patch.object(cli,'process_creation',return_value=456),patch.object(cli.Client,'request',response):
                self.assertEqual(cli.main(['--resume',str(state),'--output',str(out)]),0)
            self.assertEqual(calls,['status']);self.assertEqual(json.loads(out.read_text())['reason'],'date_limit')
    def test_reparse_or_other_process_endpoint_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);p,e=self.endpoint(root)
            with patch.object(cli,'process_creation',return_value=None):
                with self.assertRaisesRegex(RuntimeError,'exited or changed'):cli.read_endpoint(p)

class SpeedCliTests(unittest.TestCase):
    def test_old_host_refuses_acceleration_before_submission(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder)/'result.json'
            endpoint={'pid':123,'creation_filetime':456,'nonce':'test'}
            with patch.object(cli,'discover',return_value=(Path(folder)/'runtime.json',endpoint)),patch.object(cli.Client,'request') as request:
                self.assertEqual(cli.main(['--current','--speed','400','--output',str(out)]),2)
                request.assert_not_called()
                self.assertIn('does not support',json.loads(out.read_text())['reason'])
    def test_default_inherits_old_host_speed_without_claiming_control(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder)/'result.json';endpoint={'pid':123,'creation_filetime':456,'nonce':'test'};captured=[]
            def request(self,op,**fields):
                if op=='start':captured.append(fields['options']);return {'handle':42}
                return {'ok':True,'phase':'completed','complete':True,'reason':'date_limit'}
            with patch.object(cli,'discover',return_value=(Path(folder)/'runtime.json',endpoint)),patch.object(cli.Client,'request',request):
                self.assertEqual(cli.main(['--current','--output',str(out)]),0)
            self.assertEqual(captured[0]['speed_x100'],0)
    def test_invalid_speed_does_not_discover_or_submit(self):
        with patch.object(cli,'discover') as discover:
            with self.assertRaises(SystemExit):cli.main(['--current','--speed','801','--output','unused.json'])
            discover.assert_not_called()

if __name__=='__main__':unittest.main()
