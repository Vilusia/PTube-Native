"""Test the shared native resolver; never print temporary signed media URLs."""
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / 'upstream/komi-tube/vendor/tilefinch'
probe = ENGINE / 'build-preset-release/tilefinch-youtube-resolver-probe'
video_id = sys.argv[1] if len(sys.argv) > 1 else 'jNQXAC9IVRw'
if not re.fullmatch(r'[A-Za-z0-9_-]{11}', video_id):
    raise SystemExit('Provide a YouTube video ID, not a media URL')
watch = 'https://www.youtube.com/watch?v=' + video_id
try:
    result = subprocess.run([str(probe), watch, '360', '--demux-open'],
                            capture_output=True, text=True, timeout=150)
    lines = []
    for line in (result.stdout + '\n' + result.stderr).splitlines():
        if line.startswith(('video-url=', 'audio-url=')):
            continue
        lines.append(re.sub(r'https?://[^\s"<>]+', '[URL]', line))
    report = {'result': 'PASS' if result.returncode == 0 else 'FAIL',
              'scope': 'Host resolver and remote MP4 demux only; not PSP hardware',
              'details': '\n'.join(lines)[-5000:]}
except subprocess.TimeoutExpired:
    report = {'result': 'TIMEOUT', 'scope': 'Host network smoke test'}
(ROOT / 'dist').mkdir(exist_ok=True)
(ROOT / 'dist/native-live-test.json').write_text(json.dumps(report, indent=2) + '\n')
(ROOT / f'dist/native-live-{video_id}.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
raise SystemExit(0 if report['result'] == 'PASS' else 1)
