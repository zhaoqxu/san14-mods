"""Package explicitly selected default/optional Mods without source or game files."""
import argparse
import hashlib
import json
import importlib.util
from pathlib import Path, PurePosixPath
import shutil
import tempfile
from zipfile import ZIP_DEFLATED, ZipFile


def read(path):return json.loads(path.read_text('utf-8-sig'))


def discover(root):
    result=[]
    for group in ('default','optional'):
        for path in sorted((root/'mods'/group).glob('*/mod.json')):
            manifest=read(path)
            result.append({'id':manifest['id'],'name':manifest['name'],'version':manifest['version'],
                           'type':manifest['entry']['type'],'group':group,
                           'path':path.parent.relative_to(root).as_posix()})
    if len({item['id'] for item in result})!=len(result):raise ValueError('Duplicate Mod identity')
    return result


def selected(root, names=(), optional=False):
    available=discover(root)
    if names:
        unknown=set(names)-{item['id'] for item in available}
        if unknown:raise ValueError('Unknown Mod: '+', '.join(sorted(unknown)))
        return [item for item in available if item['id'] in names]
    return [item for item in available if optional or item['group']=='default']


def safe_file(root,name):
    relative=PurePosixPath(name)
    if (not name or relative.is_absolute() or '..' in relative.parts or ':' in name or '\\' in name):
        raise ValueError('Invalid package path')
    path=root.joinpath(*relative.parts)
    if not path.resolve().is_relative_to(root.resolve()) or path.is_symlink():raise ValueError('Package path escaped root')
    if not path.is_file():raise ValueError('Missing package asset: '+name)
    return path


def write_zip(path,files):
    path.parent.mkdir(parents=True,exist_ok=True)
    with ZipFile(path,'w',ZIP_DEFLATED) as archive:
        for name,data in sorted(files.items()):archive.writestr(name,data)
    with ZipFile(path) as archive:
        if archive.testzip() is not None or {name:archive.read(name) for name in archive.namelist()}!=files:
            raise ValueError('Package verification failed')


def content_files(root, project, authored, manifest):
    spec=importlib.util.spec_from_file_location('san14_content_compiler',root/'sdk/tools/content_compiler.py')
    compiler=importlib.util.module_from_spec(spec);spec.loader.exec_module(compiler)
    for path in project.rglob('*'):
        if path.is_symlink() or path.is_junction():raise ValueError('Content links refused')
    with tempfile.TemporaryDirectory() as temporary:
        stage=Path(temporary)/'content'
        shutil.copytree(project,stage)
        definition=read(safe_file(project,manifest['entry']['path']))
        native=definition.get('scenario',{}).get('native_file')
        if native:
            source=safe_file(authored,native)
            target=stage/native;target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(source,target)
        generated=compiler.compile_package(stage,manifest['entry']['path'],manifest['id'],manifest['version'])
    compiled_manifest=dict(manifest,entry={'type':'content','path':compiler.ENTRY})
    return {'mod.json':compiler.encoded(compiled_manifest),**generated}


def package(root,output,items):
    from jsonschema import Draft202012Validator
    validator=Draft202012Validator(read(root/'sdk/schemas/mod.schema.json'))
    grouped={'default':{},'optional':{}}
    records=[]
    for item in items:
        project=root/item['path'];manifest=read(project/'mod.json')
        validator.validate(manifest)
        entry=manifest['entry']['path']
        if item['type']=='content':
            files=content_files(root,project,output/'authored'/item['id'],manifest)
        else:
            files={'mod.json':(project/'mod.json').read_bytes(),entry:safe_file(project,entry).read_bytes()}
        readme=project/'README.md'
        if readme.is_file():files['README.md']=readme.read_bytes()
        archive=output/'mods'/item['group']/(item['id']+'-'+item['version']+'.s14mod')
        write_zip(archive,files)
        for name,data in files.items():grouped[item['group']]['SAN14Mods/'+item['id']+'/'+name]=data
        records.append({**item,'package':archive.relative_to(output).as_posix(),
                        'sha256':hashlib.sha256(archive.read_bytes()).hexdigest()})
    for group,files in grouped.items():
        if files:write_zip(output/('SAN14-mods-'+group+'.zip'),files)
    report={'schema_version':1,'mods':records,'default_count':sum(item['group']=='default' for item in items),
            'optional_count':sum(item['group']=='optional' for item in items)}
    (output/'packages.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False))
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1])
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--include-optional',action='store_true')
    parser.add_argument('--mod',action='append',default=[])
    parser.add_argument('--list',action='store_true')
    args=parser.parse_args()
    root=args.root.resolve();output=args.output.resolve()
    if not output.is_relative_to(root/'build'):raise ValueError('Outputs must be under build/')
    items=selected(root,args.mod,args.include_optional)
    if args.list:print(json.dumps(items,ensure_ascii=False));return
    output.mkdir(parents=True,exist_ok=True)
    package(root,output,items)


if __name__=='__main__':main()
