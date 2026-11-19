"""Run a native two-sided SAN14 campaign through the installed Host match service.

The original game stays running and uses its normal rendering and AI. A first
baseline must be created with the matching campaign Mod and saved normally.
This client never injects code, edits saves, or calls per-turn advance tools.
"""
from __future__ import annotations
import argparse, ctypes, json, os, sys, time, uuid
from pathlib import Path

def atomic_json(path: Path, value):
    path.parent.mkdir(parents=True,exist_ok=True)
    tmp=path.with_name(path.name+'.'+uuid.uuid4().hex+'.tmp')
    try:
        with tmp.open('x',encoding='utf-8',newline='\n') as stream:
            json.dump(value,stream,ensure_ascii=False,indent=2);stream.write('\n');stream.flush();os.fsync(stream.fileno())
        os.replace(tmp,path)
    finally:
        if tmp.exists():tmp.unlink()

def process_creation(pid: int):
    if sys.platform!='win32':return None
    from ctypes import wintypes as wt
    k=ctypes.WinDLL('kernel32',use_last_error=True)
    k.OpenProcess.argtypes=[wt.DWORD,wt.BOOL,wt.DWORD];k.OpenProcess.restype=wt.HANDLE
    k.GetProcessTimes.argtypes=[wt.HANDLE,ctypes.POINTER(wt.FILETIME),ctypes.POINTER(wt.FILETIME),ctypes.POINTER(wt.FILETIME),ctypes.POINTER(wt.FILETIME)]
    k.GetExitCodeProcess.argtypes=[wt.HANDLE,ctypes.POINTER(wt.DWORD)]
    k.CloseHandle.argtypes=[wt.HANDLE]
    h=k.OpenProcess(0x1000,False,pid)
    if not h:return None
    try:
        exit_code=wt.DWORD()
        if not k.GetExitCodeProcess(h,ctypes.byref(exit_code)) or exit_code.value!=259:return None
        a,b,c,d=(wt.FILETIME() for _ in range(4))
        if not k.GetProcessTimes(h,ctypes.byref(a),ctypes.byref(b),ctypes.byref(c),ctypes.byref(d)):return None
        return (a.dwHighDateTime<<32)|a.dwLowDateTime
    finally:k.CloseHandle(h)

def read_endpoint(path: Path):
    value=json.loads(path.read_text(encoding='utf-8-sig'))
    if value.get('protocol')!=1 or not value.get('available'):raise RuntimeError('Match service unavailable in this game build')
    if process_creation(value['pid'])!=value['creation_filetime']:raise RuntimeError('Game process has exited or changed')
    return value

def discover(explicit: Path|None):
    if explicit:
        p=explicit/'runtime.json' if explicit.is_dir() else explicit
        return p,read_endpoint(p)
    root=Path(os.environ.get('LOCALAPPDATA',''))/'San14ModManager'
    candidates=[]
    for p in root.glob('matches/*/runtime.json'):
        try:candidates.append((p,read_endpoint(p)))
        except (OSError,ValueError,RuntimeError):pass
    # Per-installation data roots are supported, without traversing saves or backups.
    for child in root.iterdir() if root.is_dir() else []:
        if not child.is_dir() or child.name in {'backups','matches','plugins'}:continue
        for p in child.glob('matches/*/runtime.json'):
            try:candidates.append((p,read_endpoint(p)))
            except (OSError,ValueError,RuntimeError):pass
    if len(candidates)!=1:raise RuntimeError(f'Expected one live match Host, found {len(candidates)}. Start the game with the new manager or pass --endpoint.')
    return candidates[0]

class Client:
    def __init__(self,path: Path,endpoint: dict):self.path=path;self.endpoint=endpoint
    def request(self,operation: str,timeout=15,**fields):
        if process_creation(self.endpoint['pid'])!=self.endpoint['creation_filetime']:raise RuntimeError('game_closed')
        message={k:self.endpoint[k] for k in ('protocol','pid','creation_filetime','nonce')}
        message.update(operation=operation,**fields);request_id=uuid.uuid4().hex
        incoming=self.path.parent/'inbox'/f'{request_id}.json';outgoing=self.path.parent/'outbox'/f'{request_id}.json'
        atomic_json(incoming,message);deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            if outgoing.exists():
                result=json.loads(outgoing.read_text(encoding='utf-8'));outgoing.unlink()
                if not result.get('ok'):raise RuntimeError(result.get('error',f"Host result {result.get('result')}"))
                return result
            if process_creation(self.endpoint['pid'])!=self.endpoint['creation_filetime']:raise RuntimeError('game_closed')
            time.sleep(.1)
        raise TimeoutError('Host response timed out; submission may have occurred. Resume using the saved run state; do not blindly start another match.')

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    source=p.add_mutually_exclusive_group(required=True);source.add_argument('--save',help='e.g. svdexceSC49 (without extension)');source.add_argument('--current',action='store_true',help='explicitly start the currently loaded matching campaign');source.add_argument('--resume',type=Path,help='resume observation from an existing .state.json')
    p.add_argument('--endpoint',type=Path);p.add_argument('--content',default='two-city-all-stars-mod');p.add_argument('--max-turns',type=int,default=360)
    p.add_argument('--wall-timeout',type=int,default=3600);p.add_argument('--stall-timeout',type=int,default=180);p.add_argument('--run-id',default=None)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--poll-interval',type=float,default=.5)
    a=p.parse_args(argv)
    if a.output.exists():p.error('Output already exists; select a new file to preserve earlier results')
    if a.poll_interval<.1:p.error('Poll interval must be at least 0.1 seconds')
    output=a.output.resolve();state_path=output.with_suffix(output.suffix+'.state.json')
    trace_path=output.with_suffix(output.suffix+'.jsonl');last_signature=None;client=None;handle=None;last=None
    try:
        if a.resume:
            saved=json.loads(a.resume.read_text(encoding='utf-8'));path=Path(saved['endpoint_path']);endpoint=read_endpoint(path)
            if any(endpoint[k]!=saved['endpoint'][k] for k in ('pid','creation_filetime','nonce')):raise RuntimeError('Resume belongs to a different game process')
            handle=saved.get('handle');options=saved['options']
        else:
            path,endpoint=discover(a.endpoint)
            options={'request_id':a.run_id or uuid.uuid4().hex,'save_basename':a.save or '', 'required_content_id':a.content,'max_turns':a.max_turns,'wall_timeout_seconds':a.wall_timeout,'stall_timeout_seconds':a.stall_timeout}
        client=Client(path,endpoint)
        state={'endpoint_path':str(path),'endpoint':endpoint,'options':options,'handle':handle}
        atomic_json(state_path,state) # durable identity before the mutating request
        if handle is None:
            handle=client.request('start',options=options)['handle'];state['handle']=handle;atomic_json(state_path,state)
        progress_at=time.monotonic()
        while True:
            last=client.request('status',handle=handle)
            signature=(last.get('phase'),last.get('end_date'),last.get('complete'))
            if signature!=last_signature:
                with trace_path.open('a',encoding='utf-8') as log:log.write(json.dumps(last,ensure_ascii=False)+'\n')
                print(json.dumps({k:last.get(k) for k in ('run_id','phase','start_date','end_date','reason','elapsed_wall_ms')},ensure_ascii=False),flush=True);last_signature=signature
                progress_at=time.monotonic()
            if last.get('complete'):
                atomic_json(output,last);return 0 if last['phase']=='completed' else 2
            if time.monotonic()-progress_at>max(150,options.get('stall_timeout_seconds',180)+30):
                try:client.request('cancel',handle=handle)
                except Exception:pass
                raise TimeoutError('Host stopped making progress; stop requested, outcome remains unverified')
            time.sleep(a.poll_interval)
    except KeyboardInterrupt:
        if client and handle:
            try:client.request('cancel',handle=handle)
            except Exception:pass
        print('Stop requested; resume observation using the saved state to confirm a safe stop.',file=sys.stderr);return 130
    except Exception as e:
        # Preserve the last valid sample and distinguish transport failure from
        # a verified game outcome. Never invent a winner after a disconnect.
        failure={'schema_version':1,'complete':False,'phase':'client_error','reason':str(e),'winner_force_id':None,'last_valid_status':last,'resume_state':str(state_path)}
        atomic_json(output,failure);print(json.dumps(failure,ensure_ascii=False),file=sys.stderr);return 2
if __name__=='__main__':raise SystemExit(main())
