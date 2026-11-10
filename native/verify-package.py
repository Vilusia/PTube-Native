import hashlib
import json
from pathlib import Path
import struct
import zipfile

root = Path(__file__).resolve().parents[1]
archive = root / 'dist/PTube-native-psp.zip'
prefix = 'PSP/GAME/PTUBE_NATIVE/'
with zipfile.ZipFile(archive) as package:
    assert package.testzip() is None
    names = set(package.namelist())
    assert all(name.startswith(prefix) and '..' not in Path(name).parts for name in names)
    for name in ['EBOOT.PBP', 'roots.pem', 'fonts/TilefinchSans-Regular.ttf',
                 'NOTICES/LICENSE-KOMI', 'NOTICES/LICENSE', 'BUILD-INFO.json', 'SHA256SUMS']:
        assert prefix + name in names, name
    assert not any(name.endswith(('server.txt', '.env', 'ptube-bookmarks.txt', 'ptube-wifi.txt')) for name in names)
    data = package.read(prefix + 'EBOOT.PBP')
    magic, version, *offsets = struct.unpack_from('<10I', data)
    assert magic == 0x50425000 and offsets == sorted(offsets) and offsets[-1] <= len(data)
    assert data[offsets[6]:offsets[6]+4] == b'\x7fELF'
    assert b'start app version=6 safety=search-timeout-home-exit' in data
    assert json.loads(package.read(prefix + 'BUILD-INFO.json'))['build'] == 6
    sfo = data[offsets[0]:offsets[1]]
    magic, version, keys, values, count = struct.unpack_from('<5I', sfo)
    assert magic == 0x46535000
    metadata = {}
    for i in range(count):
        key, kind, length, capacity, value = struct.unpack_from('<HHIII', sfo, 20+i*16)
        name = sfo[keys+key:].split(b'\0',1)[0].decode()
        metadata[name] = sfo[values+value:values+value+length]
    assert metadata['TITLE'].rstrip(b'\0') == b'PTube Native'
    assert struct.unpack('<I', metadata['MEMSIZE'])[0] == 1
    assert b'BEGIN CERTIFICATE' in package.read(prefix + 'roots.pem')
    for line in package.read(prefix + 'SHA256SUMS').decode().splitlines():
        digest, name = line.split('  ', 1)
        assert hashlib.sha256(package.read(prefix + name)).hexdigest() == digest, name
    print(json.dumps({'package':'PASS', 'title':'PTube Native', 'memsize':1,
                      'ebootBytes':len(data), 'files':len(names),
                      'checksum':hashlib.sha256(archive.read_bytes()).hexdigest()}, indent=2))
