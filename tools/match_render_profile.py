"""Switch SAN14 to a small render window for batch matches, then restore it.
Only WIDTH, HEIGHT and WINDOW_MODE in the game's own user configuration change.
Close the game and launcher before apply/restore; no simulation rules change.
"""
from __future__ import annotations
import argparse, ctypes, json, os, sys
from pathlib import Path
from ctypes import wintypes as wt

KEY = r'Software\KoeiTecmo\SAN14\Configs'
LOW = {'WIDTH': 1280, 'HEIGHT': 720, 'WINDOW_MODE': 1}

def processes_running():
    class Entry(ctypes.Structure):
        _fields_ = [('size',wt.DWORD),('usage',wt.DWORD),('pid',wt.DWORD),
                    ('heap',ctypes.c_size_t),('module',wt.DWORD),('threads',wt.DWORD),
                    ('parent',wt.DWORD),('priority',wt.LONG),('flags',wt.DWORD),('exe',wt.WCHAR*260)]
    k=ctypes.WinDLL('kernel32',use_last_error=True)
    k.CreateToolhelp32Snapshot.restype=wt.HANDLE
    k.Process32FirstW.argtypes=k.Process32NextW.argtypes=[wt.HANDLE,ctypes.POINTER(Entry)]
    k.CloseHandle.argtypes=[wt.HANDLE]
    handle=k.CreateToolhelp32Snapshot(2,0)
    if handle==ctypes.c_void_p(-1).value:raise OSError('Cannot enumerate SAN14 processes')
    try:
        row=Entry();row.size=ctypes.sizeof(row);ok=k.Process32FirstW(handle,ctypes.byref(row))
        while ok:
            name=row.exe.casefold()
            if name.startswith('san14') and name.endswith('.exe') and name!='san14-managed-worker.exe':return True
            ok=k.Process32NextW(handle,ctypes.byref(row))
        return False
    finally:k.CloseHandle(handle)

class Config:
    def __enter__(self):
        import winreg
        self.reg=winreg
        self.handle=winreg.OpenKey(winreg.HKEY_CURRENT_USER,KEY,0,winreg.KEY_READ|winreg.KEY_WRITE)
        return self
    def __exit__(self,*args):self.reg.CloseKey(self.handle)
    def read(self):
        values={}
        for name in LOW:
            value,kind=self.reg.QueryValueEx(self.handle,name)
            if kind!=self.reg.REG_DWORD:raise ValueError('Unexpected SAN14 configuration type: '+name)
            values[name]=value
        validate(values);return values
    def write(self,values):
        validate(values)
        for name,value in values.items():self.reg.SetValueEx(self.handle,name,0,self.reg.REG_DWORD,value)
        self.reg.FlushKey(self.handle)

def validate(values):
    if set(values)!=set(LOW) or any(type(v) is not int for v in values.values()):raise ValueError('Invalid render profile')
    if not 640<=values['WIDTH']<=16384 or not 480<=values['HEIGHT']<=16384 or values['WINDOW_MODE'] not in (0,1,2):raise ValueError('Invalid render dimensions or window mode')

def persist(path,value,exclusive=False):
    path.parent.mkdir(parents=True,exist_ok=True)
    # An apply receipt is created exclusively and flushed before any registry write.
    if exclusive:
        with path.open('x',encoding='utf-8') as f:json.dump(value,f,indent=2);f.flush();os.fsync(f.fileno())
    else:
        from run_match import atomic_json
        atomic_json(path,value)

def switch(operation,backup,config):
    if processes_running():raise RuntimeError('Close SAN14 and its launcher before changing the render profile')
    current=config.read()
    if operation=='apply':
        receipt={'schema':1,'key':KEY,'before':current,'applied':LOW,'state':'prepared'}
        persist(backup,receipt,exclusive=True)
        target=LOW
    else:
        receipt=json.loads(backup.read_text(encoding='utf-8'))
        if receipt.get('schema')!=1 or receipt.get('key')!=KEY or receipt.get('applied')!=LOW:raise ValueError('Not a supported SAN14 render backup')
        validate(receipt['before'])
        if receipt.get('state') not in ('prepared','applied','restored'):raise ValueError('Unknown render backup state')
        if current==receipt['before']:
            receipt['state']='restored';persist(backup,receipt);return receipt
        if current!=receipt['applied']:raise RuntimeError('Render settings changed since apply; refusing to overwrite them')
        target=receipt['before']
    if processes_running():raise RuntimeError('SAN14 started during preparation; no settings were written')
    try:
        config.write(target)
        if config.read()!=target:raise RuntimeError('Render settings did not round-trip')
    except Exception:
        config.write(current)
        raise
    receipt['state']='applied' if operation=='apply' else 'restored';persist(backup,receipt);return receipt

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation',choices=['status','apply','restore'])
    parser.add_argument('--backup',type=Path,help='private local receipt; required for apply/restore, never overwritten on apply')
    args=parser.parse_args(argv)
    if args.operation!='status' and args.backup is None:parser.error('--backup is required')
    if sys.platform!='win32':raise RuntimeError('Windows is required')
    with Config() as config:
        result=config.read() if args.operation=='status' else switch(args.operation,args.backup,config)
    print(json.dumps(result));return 0
if __name__=='__main__':
    try:raise SystemExit(main())
    except Exception as error:print(str(error),file=sys.stderr);raise SystemExit(2)
