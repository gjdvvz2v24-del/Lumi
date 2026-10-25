#!/usr/bin/env python3
"""Replace 1-3 Wi-Fi profiles over USB, preserving API keys and prompt.

Requires firmware with LUMI_WIFI_READY? support. Changes apply on next restart.
--check only validates input; it never opens USB. No secret is printed.
"""
import argparse
import getpass
import json
import time
from pathlib import Path

MAX_JSON=1400


def validate_profiles(profiles):
    if not isinstance(profiles,list) or not 1<=len(profiles)<=3:
        raise ValueError('Podaj od 1 do 3 sieci.')
    seen=set()
    for item in profiles:
        if not isinstance(item,dict) or not isinstance(item.get('ssid'),str) or not isinstance(item.get('password'),str):
            raise ValueError('Każda sieć wymaga pól ssid i password.')
        ssid,password=item['ssid'],item['password']
        if not 1<=len(ssid.encode())<=32 or ssid in seen:
            raise ValueError('SSID musi być różne dla każdej sieci i mieć od 1 do 32 bajtów UTF-8.')
        seen.add(ssid)
        size=len(password.encode())
        if size>63 or (size and size<8) or any(ord(c)<32 or ord(c)==127 for c in ssid+password):
            raise ValueError('Hasło musi być puste lub mieć 8-63 bajty, bez znaków sterujących.')
    payload=json.dumps(profiles,ensure_ascii=False,separators=(',',':')).encode()
    if len(payload)>MAX_JSON:
        raise ValueError('Konfiguracja przekracza limit USB.')
    return payload


def ask_profiles():
    profiles=[]
    for i in range(3):
        ssid=input(f'SSID sieci {i+1} (2,4 GHz; Enter kończy listę): ')
        if not ssid:
            break
        password=getpass.getpass('Hasło (Enter = sieć otwarta): ')
        profiles.append({'ssid':ssid,'password':password})
    return profiles


def wait_for(connection,expected,timeout=5):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        line=connection.readline().strip()
        if line==expected:
            return
        if line.startswith(b'LUMI_WIFI_') and line not in (b'LUMI_WIFI_READY',):
            raise RuntimeError('Urządzenie odrzuciło konfigurację lub jest zajęte. Zakończ rozmowę i spróbuj ponownie.')
    raise RuntimeError('Brak potwierdzenia USB. Sprawdź wersję firmware; konfiguracja nie została potwierdzona.')


def upload(port,payload):
    import serial
    # Set control lines before open to avoid intentionally resetting the device.
    with serial.Serial(port=None,baudrate=115200,timeout=.2,write_timeout=3) as connection:
        connection.dtr=False;connection.rts=False;connection.port=port;connection.open()
        connection.reset_input_buffer()
        connection.write(b'LUMI_WIFI_READY?\n');connection.flush()
        wait_for(connection,b'LUMI_WIFI_READY')
        connection.write(b'LUMI_WIFI_SET '+payload+b'\n');connection.flush()
        wait_for(connection,b'LUMI_WIFI_SAVED_RESTART_REQUIRED')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port',nargs='?',help='np. /dev/cu.usbmodem1101')
    parser.add_argument('--file',type=Path,help='Lokalny JSON: [{"ssid":"...","password":"..."}, ...]')
    parser.add_argument('--check',action='store_true',help='Sprawdź dane bez otwierania USB i zapisywania czegokolwiek')
    args=parser.parse_args()
    if not args.check and not args.port:
        parser.error('Podaj port albo --check.')
    try:
        profiles=json.loads(args.file.read_text()) if args.file else ask_profiles()
        payload=validate_profiles(profiles)
        if args.check:
            print(f'OK: poprawne profile ({len(profiles)}); nie otwarto USB.')
        else:
            upload(args.port,payload)
            print('Zapisano wyłącznie profile Wi-Fi. Zrestartuj Lumi, aby je zastosować.')
        return 0
    except (ValueError,OSError,RuntimeError,KeyboardInterrupt):
        print('Nie ukończono konfiguracji. Sprawdź dane, połączenie USB i czy Lumi jest w trybie oczekiwania.')
        return 1


if __name__=='__main__':
    raise SystemExit(main())
