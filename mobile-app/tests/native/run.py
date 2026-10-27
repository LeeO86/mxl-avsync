"""Run the actual iOS AVFoundation decoder and avsync-core on synthetic recordings.
Requires Xcode, ffmpeg, and a booted arm64 iOS simulator. No camera/mic needed.
"""
import json
import math
from pathlib import Path
import subprocess
import tempfile
import os
from contextlib import nullcontext

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent

def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)

devices = json.loads(subprocess.check_output(['xcrun', 'simctl', 'list', 'devices', 'booted', '--json']))
device = next((d['udid'] for runtime, group in devices['devices'].items() if '.iOS-' in runtime for d in group if d['state'] == 'Booted'), None)
if not device:
    raise SystemExit('Boot an iOS simulator first.')
sdk = subprocess.check_output(['xcrun', '--sdk', 'iphonesimulator', '--show-sdk-path'], text=True).strip()
module = ROOT / 'mobile-app/modules/avsync/ios'
artifact_dir=os.environ.get('AVSYNC_NATIVE_OUT')
if artifact_dir:Path(artifact_dir).mkdir(parents=True,exist_ok=True)
with (nullcontext(artifact_dir) if artifact_dir else tempfile.TemporaryDirectory(prefix='avsync-native-')) as temp:
    tmp = Path(temp)
    includes = ['-I' + str(ROOT / 'core/include'), '-I' + str(ROOT / 'third_party/pocketfft')]
    run(['clang++', '-std=c++20', '-O2', *includes, HERE / 'fixture.cpp', *[ROOT / f'core/src/{name}.cpp' for name in ['plan', 'chirp', 'marker', 'timecode', 'detect', 'g711']], '-o', tmp / 'fixture'])
    run([tmp / 'fixture', tmp])
    run(['clang++', '-std=c++20', '-O2', *includes, HERE / 'phone-stability.cpp', *sorted(module.glob('Core_*.cpp')), '-o', tmp / 'phone-stability'])
    run([tmp / 'phone-stability'])
    run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'rawvideo', '-pixel_format', 'gray', '-video_size', '640x360', '-framerate', '50', '-i', tmp / 'video.gray', '-f', 'f32le', '-ar', '48000', '-ac', '1', '-i', tmp / 'audio.f32', '-c:v', 'libx264', '-crf', '14', '-pix_fmt', 'yuv420p', '-c:a', 'aac', tmp / 'offset35.mov'])
    frameworks = [arg for framework in ['Foundation', 'UIKit', 'AVFoundation', 'CoreMedia', 'CoreVideo', 'CoreGraphics', 'Vision'] for arg in ['-framework', framework]]
    run(['xcrun', '--sdk', 'iphonesimulator', 'clang++', '-target', 'arm64-apple-ios15.1-simulator', '-isysroot', sdk, '-std=c++20', '-O2', '-fobjc-arc', '-I' + str(module), *includes, HERE / 'runner.mm', module / 'AVSyncFileAnalyzer.mm', module/'AVSyncQRCodeReader.mm', *sorted(module.glob('Core_*.cpp')), *frameworks, '-o', tmp / 'runner'])
    def analyze(path, calibration=False, expect_error=False):
        result = subprocess.run(['xcrun', 'simctl', 'spawn', device, str(tmp / 'runner'), str(path), *(['calibration'] if calibration else [])], capture_output=True, text=True)
        report = json.loads(result.stdout)
        assert bool(report.get('error')) == expect_error, report
        assert result.returncode == int(expect_error), result.stderr
        return report
    report = analyze(tmp / 'offset35.mov')
    assert math.isclose(report['offset_ms'], 35, abs_tol=1), report
    assert report['eventCount'] >= 4 and report['decodedFrames'] == 150
    run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-i', tmp / 'offset35.mov', '-vf', 'transpose=clock', '-c:v', 'libx264', '-crf', '14', '-c:a', 'copy', tmp / 'rotated.mov'])
    run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-display_rotation', '90', '-i', tmp / 'rotated.mov', '-c', 'copy', tmp / 'orientation.mov'])
    oriented = analyze(tmp / 'orientation.mov')
    assert math.isclose(oriented['offset_ms'], 35, abs_tol=1), oriented
    calibration = analyze(tmp / 'offset35.mov', calibration=True)
    assert len(calibration['frames']) > 2 and calibration['waveform']
    from urllib.parse import unquote, urlparse
    assert all(Path(unquote(urlparse(frame['uri']).path)).is_file() for frame in calibration['frames'])
    run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-i', tmp / 'offset35.mov', '-an', '-c:v', 'copy', tmp / 'silent.mov'])
    assert 'no audio' in analyze(tmp / 'silent.mov', expect_error=True)['error']
    run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi', '-i', 'color=black:s=640x360:r=50:d=3', '-i', tmp / 'offset35.mov', '-map', '0:v', '-map', '1:a', '-c:v', 'libx264', '-c:a', 'copy', '-shortest', tmp / 'no-marker.mov'])
    assert 'No matching' in analyze(tmp / 'no-marker.mov', expect_error=True)['error']
    run(['xcrun', '--sdk', 'iphonesimulator', 'clang++', '-target', 'arm64-apple-ios15.1-simulator', '-isysroot', sdk, '-std=c++20', '-O2', '-fobjc-arc', '-I' + str(module), *includes, HERE / 'live-runner.mm', module / 'AVSyncStreamAnalyzer.mm', *sorted(module.glob('Core_*.cpp')), *frameworks, '-o', tmp / 'live-runner'])
    reports = json.loads(subprocess.check_output(['xcrun', 'simctl', 'spawn', device, str(tmp / 'live-runner'), str(tmp / 'offset35.mov')], text=True))
    readings = [r for r in reports if r.get('kind') == 'reading']
    assert readings and abs(readings[-1]['offset_ms'] - 35) < 1, reports
    assert readings[-1]['diagnosticVersion'] == 3 and readings[-1]['videoEvents'] >= 3
    assert readings[-1]['audioHits'] >= 3 and readings[-1]['bestCorrelation'] > 0.9
    assert readings[-1]['picture']['found'] and readings[-1]['picture']['codeDecoded']
    assert len(readings[-1]['picture']['points']) == 4
    qr = {'s':'mxl-avsync/1','t':'native-latency','m':'LIVE','k':1,'tk':1700000037000000000,'o':0,'r':2,'f':'1080p50'}
    estimated = json.loads(subprocess.check_output(['xcrun','simctl','spawn',device,str(tmp/'live-runner'),str(tmp/'offset35.mov')], text=True, env={**os.environ, 'SIMCTL_CHILD_AVSYNC_TEST_QR':json.dumps(qr), 'SIMCTL_CHILD_AVSYNC_TEST_WALL_OFFSET_MS':'1700000000120'}))
    assert abs(estimated[-1]['latency']['video_ms']-120)<1, estimated
    assert abs(estimated[-1]['latency']['audio_ms']-155)<1, estimated
    assert estimated[-1]['latency']['estimated'] and estimated[-1]['latency']['clockAccuracy'] == 'unknown'
    run(['clang++','-std=c++20','-O2','-I'+str(module),*includes,HERE/'latency.cpp',*[ROOT/f'core/src/{n}.cpp' for n in ['plan','timecode','chirp','detect','g711']],'-o',tmp/'latency-test'])
    run([tmp/'latency-test'])
    run(['swiftc',module/'PreviewGeometry.swift',HERE/'preview-geometry.swift','-o',tmp/'preview-geometry-test'])
    run([tmp/'preview-geometry-test'])
    def trace(path):
        return json.loads(subprocess.check_output(['xcrun', 'simctl', 'spawn', device, str(tmp / 'live-runner'), str(path)], text=True))
    no_picture = trace(tmp / 'no-marker.mov')
    assert no_picture and all(row['stage'] == 'picture_missing' for row in no_picture), no_picture
    assert no_picture[-1]['audioHits'] >= 3 and no_picture[-1]['videoEvents'] == 0, no_picture
    run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-i', tmp / 'offset35.mov', '-af', 'volume=0', '-c:v', 'copy', '-c:a', 'aac', tmp / 'muted.mov'])
    muted = trace(tmp / 'muted.mov')
    assert muted and muted[-1]['stage'] == 'audio_silent', muted
    assert muted[-1]['videoEvents'] >= 3 and muted[-1]['audioHits'] == 0, muted
    run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-i', tmp / 'offset35.mov', '-vf', 'tpad=stop_mode=add:stop_duration=2:color=black', '-af', 'apad=pad_dur=2', '-c:v', 'libx264', '-crf', '14', '-c:a', 'aac', tmp / 'picture-lost.mov'])
    lost = trace(tmp / 'picture-lost.mov')
    assert lost[-1]['locatedFrames'] > 0 and lost[-1]['recentLocatedFrames'] == 0, lost
    assert lost[-1]['stage'] == 'picture_missing' and lost[-1]['kind'] == 'status', lost
    assert not lost[-1]['picture']['found'], lost
    unstable = tmp / 'unstable'; unstable.mkdir()
    run([tmp / 'fixture', unstable, '35', 'alternating'])
    run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'rawvideo', '-pixel_format', 'gray', '-video_size', '640x360', '-framerate', '50', '-i', unstable / 'video.gray', '-f', 'f32le', '-ar', '48000', '-ac', '1', '-i', unstable / 'audio.f32', '-c:v', 'libx264', '-crf', '14', '-pix_fmt', 'yuv420p', '-c:a', 'aac', unstable / 'varying.mov'])
    conflicting = json.loads(subprocess.check_output(['xcrun', 'simctl', 'spawn', device, str(tmp / 'live-runner'), str(unstable / 'varying.mov')], text=True))
    assert conflicting and not any(row['kind'] == 'reading' for row in conflicting), conflicting
    assert any(row['spread_ms'] > 100 for row in conflicting), conflicting
    calibration_reports = json.loads(subprocess.check_output(['xcrun', 'simctl', 'spawn', device, str(tmp / 'live-runner'), str(tmp / 'offset35.mov'), 'calibration'], text=True))
    take = next(r for r in calibration_reports if r.get('kind') == 'calibration')
    assert take['framesAfter'] >= 25 and take['framesBefore'] >= 25, take
    assert max(f['timeMs'] for f in take['frames']) - take['spikeTimeMs'] >= 650, take
    run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-i', tmp / 'offset35.mov', '-vf', 'fps=240', '-c:v', 'libx264', '-c:a', 'copy', tmp / 'highfps.mov'])
    high = analyze(tmp / 'highfps.mov', calibration=True)
    assert len(high['frames']) > 100 and high['framesAfter'] > 50, high
    # Production compact codec, both visual rates, and a delayed audio path.
    codec_sources=[ROOT/f'core/src/{n}.cpp' for n in ['plan','chirp','marker','qr','timecode','detect','g711']]
    run(['clang++','-std=c++20','-O2',*includes,HERE/'timecode-fixture.cpp',*codec_sources,'-o',tmp/'timecode-fixture'])
    for event_rate in (1,2):
        folder=tmp/f'timecode-{event_rate}';folder.mkdir()
        run([tmp/'timecode-fixture',folder,event_rate])
        movie=folder/'signal.mov'
        run(['ffmpeg','-v','error','-y','-f','rawvideo','-pixel_format','gray','-video_size','640x360','-framerate','50','-i',folder/'video.gray','-f','f32le','-ar','48000','-ac','1','-i',folder/'audio.f32','-c:v','libx264','-pix_fmt','yuv420p','-crf','14','-c:a','pcm_s16le',movie])
        environment={**os.environ,'SIMCTL_CHILD_AVSYNC_TEST_ADVANCE_QR':'1','SIMCTL_CHILD_AVSYNC_TEST_QR':(folder/'qr.json').read_text(),'SIMCTL_CHILD_AVSYNC_TEST_WALL_OFFSET_MS':'1700000000120'}
        coded=json.loads(subprocess.check_output(['xcrun','simctl','spawn',device,str(tmp/'live-runner'),str(movie)],text=True,env=environment))
        good=[row for row in coded if row['kind']=='reading']
        assert good and abs(good[-1]['offset_ms']-35)<1,(event_rate,coded)
        assert good[-1]['audioTimecode']['valid'] and good[-1]['audioTimecode']['crcOk'],good[-1]
        assert good[-1]['eventCount']>=3,good[-1]
        assert abs(good[-1]['latency']['video_ms']-120)<1,good[-1]
        assert abs(good[-1]['latency']['audio_ms']-155)<1,good[-1]
        assert 'audio-timecode' in good[-1]['latency']['audioSource'],good[-1]
        # No QR or test picture at the receiver: audio timecode still supplies latency.
        black=folder/'audio-only.mov'
        run(['ffmpeg','-v','error','-y','-i',movie,'-vf','drawbox=color=black:t=fill','-c:v','libx264','-c:a','copy',black])
        audio_only=json.loads(subprocess.check_output(['xcrun','simctl','spawn',device,str(tmp/'live-runner'),str(black)],text=True,env={**os.environ,'SIMCTL_CHILD_AVSYNC_TEST_WALL_OFFSET_MS':'1700000000120'}))
        decoded=[row for row in audio_only if row.get('audioTimecode',{}).get('valid')]
        assert decoded and abs(decoded[-1]['latency']['audio_ms']-155)<1,audio_only
        assert all('video_ms' not in row['latency'] for row in decoded),decoded
        assert decoded[-1]['videoEvents']==0 and not decoded[-1]['picture']['found'],decoded[-1]
    tool=os.environ.get('AVSYNC_TESTFILE_TOOL')
    if tool:
        # Actual rendered QR is recognized by Vision in the file analyzer.
        for event_rate in (1,2):
            raw=tmp/f'generated-{event_rate}.mkv';movie=tmp/f'generated-{event_rate}.mov'
            run([tool,'generate','--format','1080p50','--duration','7','--offset','35','--event-rate',event_rate,'--audio-timecode','1200','--container','matroska','--video-codec','h264','--audio-codec','pcm16','--out',raw])
            run(['ffmpeg','-v','error','-y','-i',raw,'-c:v','libx264','-pix_fmt','yuv420p','-crf','14','-c:a','pcm_s16le',movie])
            actual=analyze(movie)
            assert actual['eventCount']>=3 and abs(actual['offset_ms']-35)<1,actual
    print('PASS: compact LIVE audio timecode at 1/2 Hz, +35-ms AV, independent +120/+155-ms estimated latencies, audio-only without QR/image, actual generated FILE QR when AVSYNC_TESTFILE_TOOL is set.')
    print(f"PASS: +35 ms -> {report['offset_ms']:.6f} ms, {report['eventCount']} events; orientation metadata; live PCM16; conflicting offsets suppressed; 700-ms calibration tail; >100 frames at 240 fps; real calibration frames; missing-audio and missing-marker errors; independent chirp diagnostics; silent audio and stale picture status.")
