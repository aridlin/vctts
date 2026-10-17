#!/usr/bin/env python3
"""Provision VCTTS's private Microsoft Speech prefix; never modifies Windows."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import urllib.request

DOWNLOADS = {
    'SpeechPlatformRuntime.msi': 'https://download.microsoft.com/download/a/6/4/a64012d6-d56f-4e58-85e3-531e56abc0e6/x64_SpeechPlatformRuntime/SpeechPlatformRuntime.msi',
    'ZiraPro.msi': 'https://download.microsoft.com/download/4/0/d/40d6347a-afa5-417d-a9bb-173d937beed4/MSSpeech_TTS_en-US_ZiraPro.msi',
    'Paulina.msi': 'https://download.microsoft.com/download/4/0/d/40d6347a-afa5-417d-a9bb-173d937beed4/MSSpeech_TTS_pl-PL_Paulina.msi',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--windows-root', type=Path, required=True,
                        help='Existing Windows directory containing System32/Speech/Common/sapi.dll')
    parser.add_argument('--prefix', type=Path, default=Path.home() / '.local/share/vctts/sapi/prefix')
    parser.add_argument('--cache', type=Path, default=Path.home() / '.cache/vctts/microsoft-speech')
    args = parser.parse_args()
    native = args.windows_root / 'System32/Speech/Common/sapi.dll'
    if not native.is_file():
        parser.error(f'Microsoft SAPI runtime not found: {native}')
    if not shutil.which('wine') or not shutil.which('wineboot'):
        parser.error('Install Wine before configuring Microsoft voices')
    prefix = args.prefix.expanduser().resolve()
    if not prefix.is_absolute() or prefix == args.windows_root.resolve():
        parser.error('Use a separate, private Wine prefix')
    if prefix.exists() and (prefix / 'system.reg').exists():
        parser.error(f'Prefix already exists: {prefix}; choose a new path to avoid changing another Wine application')
    bridge = prefix.parent / 'speech11-bridge.exe'
    if not bridge.is_file():
        parser.error(f'Build/install speech11-bridge.exe first: {bridge}')
    args.cache.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.update(WINEPREFIX=str(prefix), WINEDEBUG='-all',
               WINEDLLOVERRIDES='winemenubuilder.exe=d;sapi,msttsengine,msttsengine_onecore,msvcp110_win=n,b')

    def run(*command):
        subprocess.run(command, env=env, check=True, timeout=180)

    run('wineboot', '-u')
    system = prefix / 'drive_c/windows/system32'
    destination = system / 'speech/common'
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(native, destination / 'sapi.dll')
    shutil.copy2(native, system / 'sapi.dll')
    support = args.windows_root / 'System32/msvcp110_win.dll'
    if support.is_file():
        shutil.copy2(support, system / 'msvcp110_win.dll')
    run('wine', 'regsvr32', '/s', r'C:\windows\system32\speech\common\sapi.dll')
    for name, url in DOWNLOADS.items():
        target = args.cache / name
        if not target.is_file():
            temporary = target.with_suffix('.download')
            print(f'Downloading {name} from Microsoft', flush=True)
            urllib.request.urlretrieve(url, temporary)
            temporary.replace(target)
        log = args.cache / (name + '.log')
        run('wine', 'msiexec', '/i', str(target.resolve()), '/qn', '/norestart', '/L*v', str(log.resolve()))
    run('wine', str(bridge), '--list')
    print(f'Microsoft Zira Pro and Paulina installed in {prefix}')
    print('Select the Microsoft voice in VCTTS. No system audio defaults were changed.')


if __name__ == '__main__':
    main()
