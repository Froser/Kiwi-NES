# Copyright (C) 2024 Yisi Yu
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
import json
import shutil
import sys
import zipfile
from pathlib import Path

# Usage:
# Copies wasm files to 'public' folder.
# update.py full-kiwi-build-path-to-the-build-dir
# Example:
# python3 update.py /Users/user/Kiwi-Machine/cmake-build-emscripten_debug

build_dir = sys.argv[1]
rom_id = 0

def CopyFile(src, dest):
    print("Copying ", src, " to ", dest)
    shutil.copy(src, dest)


def ExtractAllTo(src, dest):
    for f in sorted(Path(src).iterdir()):
        ExtractTo(f, dest + '/' + f.stem)


def ExtractTo(src, dest):
    if src.is_file() and src.suffix == '.zip':
        print('Extracting ', src.absolute(), ' to ', dest)
        with zipfile.ZipFile(src.absolute(), 'r', zipfile.ZIP_DEFLATED) as zip_file:
            zip_file.extractall(dest)
    elif src.is_dir():
        print('Entering ', src)
        ExtractAllTo(src, dest)


def GenerateDatabase(dir):
    db_file = Path(dir + '/db.json')
    print('Generating database file: ', db_file)
    db = []
    AddManifestFromDir(db, dir, dir)

    print(db_file, 'Generated.')
    with open(db_file.absolute(), 'w', encoding='utf-8') as db_f:
        json.dump(db, db_f, ensure_ascii=False, indent=1)


def AddManifestFromDir(db, relpath, file_or_dir):
    for f in sorted(Path(file_or_dir).iterdir()):
        AddManifestToJson(db, file_or_dir, f)


def AddManifestToJson(db, relpath, file_or_dir):
    global rom_id
    if file_or_dir.is_file() and file_or_dir.name == 'manifest.json':
        # If a directory has manifest.json, this is what we want.
        # Load manifest, and set it to the database json file.
        print('Found manifest', file_or_dir.absolute())

        # Read manifest.json
        manifest_data = {}
        with open(file_or_dir.absolute(), 'r', encoding='utf-8') as f:
            manifest_data = json.load(f)
        manifest_data_title = manifest_data['titles']

        # Separate ROM details to database.
        for title in manifest_data_title:
            if title == 'default':
                manifest_data_title['default']['id'] = rom_id
                manifest_data_title['default']['name'] = file_or_dir.parent.name
                db.append(manifest_data_title['default'])
            else:
                manifest_data_title[title]['id'] = rom_id
                manifest_data_title[title]['name'] = title
                manifest_data_title[title]['dir'] = file_or_dir.parent.name
                db.append(manifest_data_title[title])
            rom_id += 1
    elif file_or_dir.is_dir():
        AddManifestFromDir(db, relpath, file_or_dir)


def main():
    # Copy frontend files
    CopyFile(build_dir + '/src/client/kiwi_machine/kiwi_machine.html', './kiwi-machine/public')
    CopyFile(build_dir + '/src/client/kiwi_machine/kiwi_machine.js', './kiwi-machine/public')
    CopyFile(build_dir + '/src/client/kiwi_machine/kiwi_machine.wasm', './kiwi-machine/public')
    CopyFile(build_dir + '/src/client/kiwi_machine/kiwi_machine.worker.js', './kiwi-machine/public')
    print('Done')

    # Copy roms
    ExtractAllTo('../kiwi_machine_core/build/nes', './kiwi-machine/public/roms')

    # Generate database json
    GenerateDatabase('./kiwi-machine/public/roms')


if __name__ == "__main__":
    main()
