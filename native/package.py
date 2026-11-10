"""Package the built native app, full notices, provenance and relink sources."""
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import zipfile
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / 'upstream/komi-tube'
ENGINE = UPSTREAM / 'vendor/tilefinch'
PIN = '47bf5677a5254dfc37d8716233e7b1861c51698d'


def main():
    actual = subprocess.check_output(['git', '-C', str(UPSTREAM), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != PIN:
        raise SystemExit('Upstream commit does not match the PTube source lock')
    source = ENGINE / 'build-preset-psp/ptube-app'
    if not (source / 'EBOOT.PBP').is_file():
        raise SystemExit('Build ptube-app-elf first')
    release = ROOT / 'dist'
    release.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ptube-native-', dir=release) as temporary:
        stage = Path(temporary)
        app = stage / 'PSP/GAME/PTUBE_NATIVE'
        app.mkdir(parents=True)
        for name in ['EBOOT.PBP', 'roots.pem', 'fonts/TilefinchSans-Regular.ttf']:
            target = app / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source / name, target)
        for licence in (ENGINE / 'fonts').glob('LICENSE-*'):
            shutil.copy2(licence, app / 'fonts' / licence.name)
        notices = app / 'NOTICES'
        shutil.copytree(ENGINE / 'third_party/notices', notices)
        for name in ['LICENSE', 'THIRD_PARTY_NOTICES.md']:
            shutil.copy2(ENGINE / name, notices / name)
        shutil.copytree(ENGINE / 'third_party/public_suffix', notices / 'public_suffix')
        for original, target in [(ENGINE / 'third_party/POCKETSPHINX_LICENSE.txt', 'pocketsphinx/POCKETSPHINX_LICENSE.txt'),
                                 (ENGINE / 'psp-assets/voice-model/en-us/README', 'voice-model/ALPHA_CEPHEI_LICENSE.txt')]:
            (notices / target).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(original, notices / target)
        (notices / 'fonts').mkdir(exist_ok=True)
        for licence in (app / 'fonts').glob('LICENSE-*'):
            shutil.copy2(licence, notices / 'fonts' / licence.name)
        shutil.copy2(ROOT / 'native/LICENSE-KOMI', notices / 'LICENSE-KOMI')
        shutil.copy2(ROOT / 'native/UPSTREAM.md', app / 'UPSTREAM.md')
        shutil.copy2(ROOT / 'docs/NATIVE-SETUP.md', app / 'README.md')
        shutil.copy2(ROOT / 'docs/NATIVE-TESTING.md', app / 'TESTING.md')
        icon = Image.new('RGB', (144, 80), '#102236')
        draw = ImageDraw.Draw(icon)
        draw.rounded_rectangle((8, 14, 58, 66), radius=10, fill='#df2935')
        draw.polygon([(25, 26), (25, 54), (45, 40)], fill='white')
        draw.text((65, 22), 'PTube', font=ImageFont.load_default(size=20), fill='white')
        draw.text((67, 48), 'NATIVE', font=ImageFont.load_default(size=10), fill='#92d5ff')
        icon_path = stage / 'ICON0.PNG'
        icon.save(icon_path)
        spec = importlib.util.spec_from_file_location('komi_packaging', UPSTREAM / 'scripts/package.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.set_pbp_title(app / 'EBOOT.PBP', 'PTube Native', icon_path.read_bytes(), memsize=1)
        metadata = {
            'name': 'PTube Native', 'upstream': str(PIN), 'engine': 'KOMI Tube / Tilefinch',
            'playback': 'Direct PSP HTTPS, H.264/AAC firmware decoding, 360p preference',
            'requires_vps': False, 'requires_data_api_key': False,
            'hardware_verified_here': False,
            'build': 5,
            'engine_local_patch': 'scheduler-error-overlap (bounded memmove)',
        }
        (app / 'BUILD-INFO.json').write_text(json.dumps(metadata, indent=2) + '\n')
        # Validate the complete notice manifest, accounting for our flat app layout.
        manifest = (ENGINE / 'cmake/PspNoticesManifest.cmake').read_text()
        for entry in re.findall(r'^\s*((?:NOTICES|slot-a/fonts)/[^\s#)]+)', manifest, re.M):
            path = app / entry.removeprefix('slot-a/')
            if not path.is_file():
                raise SystemExit('Required notice missing: ' + entry)
        data = (app / 'EBOOT.PBP').read_bytes()
        if struct.unpack_from('<I', data)[0] != 0x50425000:
            raise SystemExit('Invalid EBOOT header')
        checksums = [f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(app).as_posix()}'
                     for p in sorted(app.rglob('*')) if p.is_file()]
        (app / 'SHA256SUMS').write_text('\n'.join(checksums) + '\n')
        archive = release / 'PTube-native-psp.zip'
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as output:
            for file in sorted(app.rglob('*')):
                if file.is_file():
                    output.write(file, file.relative_to(stage))
        print('Install archive:', archive)
        shutil.copy2(archive, release / 'PTube-native-build5.zip')
    # Include the exact upstream sources and our build/adaptation code so the
    # linked libraries and frontend can be rebuilt rather than shipping a blob.
    upstream_archive = ROOT / 'native/upstream-source.tar'
    subprocess.run(['git', '-C', str(UPSTREAM), 'archive', '--format=tar',
                    '--output=' + str(upstream_archive), PIN], check=True)
    with zipfile.ZipFile(release / 'PTube-native-source.zip', 'w', zipfile.ZIP_DEFLATED) as output:
        output.write(upstream_archive, 'native/upstream-source.tar')
        for file in sorted((ROOT / 'native').rglob('*')):
            if file.is_file() and file.suffix in ('.c', '.h', '.cmake', '.mjs', '.sh', '.py', '.md', '.json'):
                if 'test-results' not in file.parts and '__pycache__' not in file.parts:
                    output.write(file, file.relative_to(ROOT))
        for name in ['Dockerfile', 'Dockerfile.host', '.dockerignore', 'LICENSE-KOMI', 'LICENSE-TILEFINCH']:
            output.write(ROOT / 'native' / name, 'native/' + name)
        output.write(ROOT / 'docs/NATIVE-SETUP.md', 'docs/NATIVE-SETUP.md')
        output.write(ROOT / 'docs/NATIVE-TESTING.md', 'docs/NATIVE-TESTING.md')
        output.write(ROOT / 'scripts/build-native.ps1', 'scripts/build-native.ps1')
    print('Source archive:', release / 'PTube-native-source.zip')


if __name__ == '__main__':
    main()
