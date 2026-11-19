"""Stage a separately built local binary adapter without changing the release lock."""
import argparse
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'manager/tools'))
from adapter_package import validate_adapter


def stage(source, output):
    source, output = source.resolve(strict=True), output.resolve()
    if not output.is_relative_to(ROOT / 'build') or output.is_relative_to(source) or source.is_relative_to(output):
        raise ValueError('Select an independent output under build/')
    validate_adapter(source)
    for path in source.rglob('*'):
        if path.is_symlink() or path.is_junction():
            raise ValueError('Adapter links refused')
    for name in ('README.txt', 'THIRD-PARTY-NOTICES.txt', 'licenses'):
        if not (source / name).exists():
            raise ValueError('Missing adapter notice: ' + name)
    if output.exists():
        raise ValueError('Select a fresh adapter output')
    shutil.copytree(source, output)
    validate_adapter(output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    stage(args.source, args.output)
