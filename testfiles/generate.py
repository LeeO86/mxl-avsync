#!/usr/bin/env python3
"""Generate and verify the curated clips with the repository's real file tools."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
OUT = Path(__file__).resolve().parent
CASES = [
    ('1080p50_h264_aac_sync.mp4', '1080p50', 0, 'mp4', 'h264', 'aac', 10),
    ('1080p50_h264_aac_audio_late_100ms.mp4', '1080p50', 100, 'mp4', 'h264', 'aac', 10),
    ('1080p50_h264_aac_audio_early_100ms.mp4', '1080p50', -100, 'mp4', 'h264', 'aac', 10),
    ('1080p25_h264_aac_sync.mp4', '1080p25', 0, 'mp4', 'h264', 'aac', 10),
    ('1080p59.94_h264_aac_sync.mp4', '1080p59.94', 0, 'mp4', 'h264', 'aac', 10),
    ('720p50_prores_pcm24_sync.mov', '720p50', 0, 'mov', 'prores', 'pcm24', 6),
    ('1080p50_h264_pcm16_audio_late_35ms.mkv', '1080p50', 35, 'matroska', 'h264', 'pcm16', 10),
    ('2160p50_hevc_aac_sync.mp4', '2160p50', 0, 'mp4', 'hevc', 'aac', 6),
]

def execute(args, log):
    try:
        subprocess.run([str(x) for x in args], check=True, stdout=log, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as error:
        log.flush()
        tail = Path(log.name).read_text(errors='replace')[-4000:]
        raise RuntimeError(f'Generator/encoder failed: {error}\n{tail}') from error

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=ROOT / 'build/testfiles/avsync-testfile-tool')
    parser.add_argument('--force', action='store_true', help='replace only the named curated clips')
    args = parser.parse_args()
    if not args.tool.is_file():
        parser.error(f'Build the generator first: {args.tool}')
    if not shutil.which('ffmpeg') or not shutil.which('ffprobe'):
        parser.error('ffmpeg and ffprobe must be on PATH')
    existing = [OUT / case[0] for case in CASES if (OUT / case[0]).exists()]
    if existing and not args.force:
        parser.error('Curated clips already exist; use --force to regenerate them')
    verification = OUT / 'verification'
    verification.mkdir(exist_ok=True)
    index = {'schema': 'mxl-avsync-testfiles/1', 'nominal_event_rate_hz': 2, 'audio_rate_hz': 48000,
             'sign_convention': 'positive means audio is late', 'files': []}
    with tempfile.TemporaryDirectory(prefix='avsync-testfiles-') as temp:
        temp = Path(temp)
        for filename, raster, offset, container, video, audio, duration in CASES:
            print(f'Generating {filename}', flush=True)
            target = OUT / filename
            transcode = not (video == 'prores' and audio == 'pcm24' and container == 'mov')
            source = temp / (filename + '.master.mov') if transcode else target
            with (temp / (filename + '.log')).open('w') as log:
                execute([args.tool, 'generate', '--format', raster, '--duration', duration,
                         '--offset', offset, '--container', 'mov' if transcode else container,
                         '--video-codec', 'prores' if transcode else video,
                         '--audio-codec', 'pcm24' if transcode else audio, '--test-id', 'TEST-' + filename.rsplit('.', 1)[0],
                         '--out', source], log)
                manifest_path = Path(str(target) + '.manifest.json')
                if transcode:
                    execute(['ffmpeg', '-hide_banner', '-y', '-i', source, '-c:v', 'libx265' if video == 'hevc' else 'libx264',
                             '-preset', 'fast', '-crf', '23' if video == 'hevc' else '18',
                             '-pix_fmt', 'yuv420p', *(['-tag:v', 'hvc1'] if video == 'hevc' else ['-profile:v', 'high']), '-c:a', 'aac' if audio == 'aac' else 'pcm_s16le',
                             *(['-b:a', '192k', '-movflags', '+faststart'] if container == 'mp4' else []), target], log)
                    manifest = json.loads(Path(str(source) + '.manifest.json').read_text())
                    manifest['video']['codec'] = video
                    manifest['video']['pixel_format'] = 'yuv420p'
                    manifest['audio']['codec'] = audio
                    manifest['container']['format'] = container
                    manifest['container']['checks'] = {'ok': None, 'notes': ['Compatibility transcode: see verification report and ffprobe metadata in index.json; source checks superseded.']}
                    manifest['transcode'] = {'source_video_codec': 'prores', 'source_audio_codec': 'pcm24', 'encoder': 'libx265' if video == 'hevc' else 'libx264', 'audio': 'encoded from the PCM reference'}
                    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
                report_path = verification / (filename + '.json')
                execute([args.tool, 'analyze', '--input', target, '--manifest', manifest_path,
                         '--json', report_path], log)
            report = json.loads(report_path.read_text())
            stats = report['statistics']['window']
            measured, count = stats.get('median'), stats.get('valid', 0)
            if measured is None or count < 3 or abs(measured - offset) > 2:
                raise RuntimeError(f'{filename}: expected {offset} ms, measured {measured}, valid={count}')
            probe = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-show_streams',
                                                        '-show_format', '-of', 'json', str(target)], text=True))
            streams = [{key: stream[key] for key in ['codec_type', 'codec_name', 'width', 'height',
                                                     'r_frame_rate', 'sample_rate', 'channels', 'pix_fmt'] if key in stream}
                       for stream in probe['streams']]
            plan = json.loads(manifest_path.read_text())
            period = plan['events'][1]['video_timeline_ms'] - plan['events'][0]['video_timeline_ms']
            size = target.stat().st_size
            if size >= 95 * 1024 * 1024:
                raise RuntimeError(f'{filename} is too large for an ordinary Git fixture: {size} bytes')
            index['files'].append({'file': filename, 'format': raster, 'duration_seconds': float(probe['format']['duration']),
                                   'expected_offset_ms': offset, 'measured_offset_ms': measured,
                                   'valid_events': count, 'event_period_ms': period, 'actual_event_rate_hz': 1000 / period, 'size_bytes': size, 'streams': streams,
                                   'sha256': hashlib.file_digest(target.open('rb'), 'sha256').hexdigest(),
                                   'manifest': manifest_path.name, 'verification': str(report_path.relative_to(OUT))})
            print(f'  Verified {measured:.3f} ms; {count} events; {size / 1024 / 1024:.1f} MiB', flush=True)
            (OUT / 'index.json').write_text(json.dumps(index, indent=2) + '\n')

if __name__ == '__main__':
    main()
