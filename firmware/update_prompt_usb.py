#!/usr/bin/env python3
"""Upload only prompt.txt on a device with the v2.4 partition layout."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent
MAX_BYTES = 15000
OFFSET = 0x7e0000
SIZE = 0x20000


def read_prompt(path):
    text = Path(path).read_text(encoding='utf-8-sig').strip()
    data = text.encode('utf-8')
    if not data or len(data) > MAX_BYTES:
        raise ValueError('Prompt musi zawierać od 1 do 15000 bajtów UTF-8.')
    if any(ord(c) < 32 and c not in '\n\r\t' for c in text):
        raise ValueError('Prompt zawiera niedozwolone znaki sterujące.')
    return data


def verify_layout(actual, expected):
    if len(expected) != 0xc00 or actual != expected:
        raise ValueError('Inny układ partycji urządzenia. Najpierw wgraj pełny firmware v2.4 z tej paczki. Nic nie zapisano.')


def update(port, path, runner=subprocess.run):
    data = read_prompt(path)
    table = (ROOT / 'dist/partition-table.bin').read_bytes()
    config = json.loads((ROOT / 'dist/prompt-storage.json').read_text())
    if config['offset'] != OFFSET or config['size'] != SIZE:
        raise ValueError('Nieobsługiwany układ partycji promptu.')
    idf = Path(os.environ.get('IDF_PATH', str(ROOT.parent / 'esp-idf')))
    generator = idf / 'components/spiffs/spiffsgen.py'
    if not generator.is_file():
        raise ValueError('Brak ESP-IDF: uruchom source /ścieżka/do/esp-idf/export.sh')
    with tempfile.TemporaryDirectory(prefix='bober-prompt-') as tmp:
        tmp = Path(tmp)
        fs = tmp / 'files'; fs.mkdir()
        (fs / 'prompt.txt').write_bytes(data)
        image = tmp / 'promptfs.bin'
        runner([sys.executable, str(generator), hex(SIZE), str(fs), str(image), *config['generator_args']], check=True)
        if image.stat().st_size != SIZE:
            raise ValueError('Nieprawidłowy rozmiar obrazu promptu.')
        base = [sys.executable, '-m', 'esptool', '--chip', 'esp32s3', '--port', port, '--baud', '115200']
        actual = tmp / 'partition-table.bin'
        # Stay in bootloader between the read and write. Never touch NVS.
        runner([*base, '--after', 'no_reset', 'read_flash', '0x8000', '0xc00', str(actual)], check=True)
        verify_layout(actual.read_bytes(), table)
        runner([*base, '--before', 'no_reset', 'write_flash', hex(OFFSET), str(image)], check=True)
    print('OK: zapisano tylko prompt.txt; Wi-Fi, klucze i aplikacja bez zmian. Urządzenie uruchomi się ponownie.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port', help='np. /dev/cu.usbmodem1101')
    parser.add_argument('file', nargs='?', type=Path, default=ROOT / 'prompt.txt')
    args = parser.parse_args()
    try:
        update(args.port, args.file)
    except (OSError, ValueError, subprocess.CalledProcessError, KeyboardInterrupt) as error:
        print(f'BŁĄD: {error}', file=sys.stderr)
        print('Jeśli urządzenie pozostało w trybie flashowania, odłącz i podłącz USB.', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
