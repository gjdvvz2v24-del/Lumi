#!/usr/bin/env python3
"""Package a prepared release under the stable firmware/ ZIP directory."""
import argparse
from pathlib import Path
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('release_dir', type=Path)
    parser.add_argument('output_zip', type=Path)
    args = parser.parse_args()
    source = args.release_dir.resolve()
    output = args.output_zip.resolve()
    if not (source / 'dist/flash_prebuilt.sh').is_file():
        parser.error('Expected a prepared release with dist/flash_prebuilt.sh')
    if output.is_relative_to(source):
        parser.error('Output ZIP must be outside the release directory')
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(source.rglob('*')):
            if path.is_file() and not any(p in {'.DS_Store', '__pycache__'} for p in path.relative_to(source).parts):
                archive.write(path, Path('firmware') / path.relative_to(source))
    with zipfile.ZipFile(output) as archive:
        assert archive.testzip() is None
        assert all(name.startswith('firmware/') for name in archive.namelist())
    print(output)


if __name__ == '__main__':
    main()
