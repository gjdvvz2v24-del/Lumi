#!/usr/bin/env python3
"""One-time USB provisioning for Lumi AtomS3R / ESP-IDF 5.4.x.

Generates a temporary NVS image with the 'bober' namespace, then flashes ONLY
its data partition. ESP32 never opens a configuration access point.
"""

import argparse
import csv
import json
import getpass
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent
DEFAULT_VOICE = json.loads((ROOT / "agent-profile.json").read_text())["tts"]["voice_id"]


def nvs_layout():
    """Read the actual NVS offset/size from this project's partition table."""
    with (ROOT / "partitions.csv").open(encoding="utf-8") as f:
        rows = (line for line in f if line.strip() and not line.lstrip().startswith("#"))
        for row in csv.reader(rows):
            fields = [v.strip() for v in row]
            if len(fields) >= 5 and fields[0] == "nvs" and fields[1:3] == ["data", "nvs"]:
                return int(fields[3], 0), int(fields[4], 0)
    raise RuntimeError("Partition 'nvs' not found in partitions.csv")


def ask_values():
    ssid = input("SSID Wi-Fi 2,4 GHz: ").strip()
    if not ssid:
        raise ValueError("SSID nie moze byc puste")
    if len(ssid.encode("utf-8")) > 31:
        raise ValueError("SSID moze miec maksymalnie 31 bajtow UTF-8 w tej wersji")

    password = getpass.getpass("Haslo Wi-Fi (Enter = siec otwarta): ")
    if len(password.encode("utf-8")) > 63:
        raise ValueError("Haslo Wi-Fi moze miec maksymalnie 63 bajty")
    if password and len(password.encode("utf-8")) < 8:
        raise ValueError("Haslo WPA2 musi miec co najmniej 8 bajtow")

    key_file = Path.home()/".config/bober/openai.key"
    if key_file.is_file():
        openai = key_file.read_text().strip()
        print("OpenAI key: odczytano lokalny ~/.config/bober/openai.key (wartość ukryta)")
    else:
        openai = getpass.getpass("OpenAI API key: ").strip()
    key = getpass.getpass("ElevenLabs API key (tylko TTS): ").strip()
    voice = input(f"Voice ID [Enter = Lumi {DEFAULT_VOICE}]: ").strip() or DEFAULT_VOICE
    for label, value, maximum in (("OpenAI key",openai,511),("ElevenLabs key",key,191),("Voice ID",voice,95)):
        if not value or len(value.encode()) > maximum or any(ord(c)<33 or ord(c)>126 for c in value):
            raise ValueError(f"Niepoprawne pole {label}")
    if not all(c.isalnum() or c in "_-" for c in voice):
        raise ValueError("Niepoprawny Voice ID")
    return {"ssid":ssid,"pass":password,"openai":openai,"key":key,"voice":voice}



def flash(port: str, values: dict[str, str]):
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        raise RuntimeError("Brak IDF_PATH. Najpierw: source ~/esp/esp-idf/export.sh")
    generator = Path(idf_path) / "components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py"
    if not generator.is_file():
        raise RuntimeError(f"Nie znaleziono generatora NVS: {generator}")
    offset, size = nvs_layout()
    if offset != 0x9000 or size != 0x6000:
        raise RuntimeError("Zmienil sie uklad NVS; sprawdz partycje przed flashowaniem")
    with tempfile.TemporaryDirectory(prefix="bober-usb-nvs-") as directory:
        csv_file = Path(directory) / "config.csv"
        bin_file = Path(directory) / "nvs.bin"
        # NVS CSV requires a namespace first; generated string fields have no shell escaping.
        with csv_file.open("w", encoding="utf-8", newline="") as f:
            writer = csv.writer(f, lineterminator="\n")
            writer.writerow(["key", "type", "encoding", "value"])
            writer.writerow(["bober", "namespace", "", ""])
            for k in ("ssid", "pass", "openai", "key", "voice"):
                writer.writerow([k, "data", "string", values[k]])
        os.chmod(csv_file, 0o600)
        print("\nGeneruje obraz NVS w tymczasowym katalogu...")
        subprocess.run([sys.executable, str(generator), "generate", str(csv_file), str(bin_file), hex(size)], check=True)
        if bin_file.stat().st_size != size:
            raise RuntimeError("Niepoprawny rozmiar wygenerowanej partycji NVS")
        print("Zapisuje TYLKO NVS przez USB (bez kompilacji i bez AP)...")
        subprocess.run([
            sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", port,
            "--baud", "115200", "write_flash", hex(offset), str(bin_file)
        ], check=True)
    print("\nOK. Dane w NVS. Atom sam startuje jako klient Twojego Wi-Fi.")
    print("Logi: idf.py -p", port, "-b 115200 monitor")
    print("UWAGA: Ponowne uruchomienie configure_usb.py nadpisuje cala konfiguracje Bober w NVS.")


def main():
    parser = argparse.ArgumentParser(description="Konfiguracja Lumi w NVS przez USB, bez Wi-Fi AP")
    parser.add_argument("port", help="np. /dev/cu.usbmodemXXXX")
    args = parser.parse_args()
    print("BOBER — konfiguracja USB; zamknij 'idf.py monitor' przed rozpoczeciem.")
    print("Potrzebne sa ponownie WSZYSTKIE wartosci, poniewaz NVS jest nadpisywany.\n")
    try:
        values = ask_values()
        flash(args.port, values)
    except (ValueError, RuntimeError, subprocess.CalledProcessError, KeyboardInterrupt) as exc:
        print(f"\nBLAD: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
