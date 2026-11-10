"""Produce PTV1: bounded JPEG frames and synchronized 44.1kHz stereo PCM.

This intentionally simple alpha transport avoids depending on an untested MP4
demuxer on the PSP. It is not the official YouTube embedded-player path.
"""
import argparse
import os
import struct
import subprocess
import tempfile
from pathlib import Path

SAMPLES = 2944  # multiple of PSP audio hardware's 64-sample alignment
FPS = f"44100/{SAMPLES}"
MAX_BYTES = 256 * 1024 * 1024


def run(args):
    subprocess.run(args, check=True, stdin=subprocess.DEVNULL, timeout=1200)


def pack(jpeg_path, audio_path, output):
    count = 0
    with open(jpeg_path, 'rb') as video, open(audio_path, 'rb') as audio, open(output, 'wb') as out:
        # magic, width, height, rate, samples/frame, reserved x3
        out.write(struct.pack('<4s7I', b'PTV1', 640, 360, 44100, SAMPLES, 0, 0, 0))
        pending = bytearray()
        eof = False
        while True:
            end = pending.find(b'\xff\xd9')
            if end < 0:
                block = video.read(65536)
                if not block:
                    eof = True
                    break
                pending.extend(block)
                if len(pending) > 524288:
                    raise ValueError('JPEG frame exceeds transport limit')
                continue
            frame = bytes(pending[:end+2])
            del pending[:end+2]
            if not frame.startswith(b'\xff\xd8'):
                raise ValueError('Invalid JPEG framing')
            pcm = audio.read(SAMPLES * 4)
            if not pcm:
                break
            pcm = pcm.ljust(SAMPLES * 4, b'\0')
            out.write(struct.pack('<I', len(frame)))
            out.write(frame)
            out.write(pcm)
            count += 1
            if out.tell() > MAX_BYTES:
                raise ValueError('Prepared video exceeds size limit')
        if eof and pending:
            raise ValueError('Truncated JPEG output')
        if count == 0:
            raise ValueError('No playable frames')
        out.write(struct.pack('<I', 0))
    return count


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--video', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--demo', action='store_true')
    args = parser.parse_args()
    output = Path(args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f'job-{args.video}-', dir=output.parent) as work:
        work = Path(work)
        if args.demo:
            run(['ffmpeg', '-v', 'error', '-filter_threads', '1', '-filter_complex_threads', '1', '-f', 'lavfi', '-i', 'testsrc2=size=640x360:rate=15',
                 '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=44100', '-t', '8',
                 '-c:v', 'mpeg4', '-threads', '1', '-c:a', 'aac', str(work / 'source.mp4')])
            source = work / 'source.mp4'
        else:
            import yt_dlp
            if len(args.video) != 11 or not all(c.isascii() and (c.isalnum() or c in '_-') for c in args.video):
                raise ValueError('Invalid video ID')
            def limit_download(progress):
                if progress.get('downloaded_bytes', 0) > 128*1024*1024:
                    raise yt_dlp.utils.DownloadError('Source exceeds size limit')

            options = {
                'format': 'best[height<=360][ext=mp4]/best[height<=360]',
                'outtmpl': str(work / 'source.%(ext)s'),
                'noplaylist': True, 'quiet': True, 'no_warnings': True, 'noprogress': True,
                'socket_timeout': 20, 'retries': 2, 'max_filesize': 128*1024*1024,
                'js_runtimes': {'node': {}},
                'progress_hooks': [limit_download],
                'match_filter': lambda info, *, incomplete=False: (
                    'Video must have a known duration of at most ten minutes'
                    if not incomplete and (not info.get('duration') or info['duration'] > 600 or info.get('is_live')) else None),
            }
            with yt_dlp.YoutubeDL(options) as ydl:
                info = ydl.extract_info('https://www.youtube.com/watch?v=' + args.video, download=True)
                if not info:
                    raise ValueError('Video unavailable')
                source = Path(ydl.prepare_filename(info))
            if not source.is_file() or source.stat().st_size > 128*1024*1024:
                raise ValueError('Source unavailable or too large')
        # Probe audio so silent videos also work.
        probe = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'a:0', '-show_entries',
                                'stream=index', '-of', 'csv=p=0', str(source)],
                               capture_output=True, check=True, timeout=30)
        audio_map = '0:a:0' if probe.stdout.strip() else '1:a:0'
        video = work / 'frames.mjpg'
        audio = work / 'audio.pcm'
        run(['ffmpeg', '-v', 'error', '-y', '-filter_threads', '1', '-filter_complex_threads', '1', '-threads', '1', '-i', str(source), '-f', 'lavfi', '-i',
             'anullsrc=r=44100:cl=stereo', '-map', '0:v:0', '-t', '600',
             '-vf', f'fps={FPS},scale=640:360:force_original_aspect_ratio=decrease,pad=640:360:(ow-iw)/2:(oh-ih)/2',
             '-c:v', 'mjpeg', '-q:v', '8', '-threads', '1', '-fs', str(MAX_BYTES), '-f', 'mjpeg', str(video),
             '-map', audio_map, '-t', '600' if audio_map.startswith('0') else str(
                 float(subprocess.check_output(['ffprobe', '-v', 'error', '-show_entries', 'format=duration',
                       '-of', 'default=nw=1:nk=1', str(source)], timeout=30).strip())),
             '-ar', '44100', '-ac', '2', '-c:a', 'pcm_s16le', '-fs', str(128*1024*1024), '-f', 's16le', str(audio)])
        temporary = str(output) + '.tmp'
        try:
            pack(video, audio, temporary)
            os.replace(temporary, output)
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)


if __name__ == '__main__':
    main()
