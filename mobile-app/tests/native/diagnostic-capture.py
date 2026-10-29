"""Verify opt-in, bounded raw capture and exact replay of an existing fixture.
Run the regular native suite first, then pass its timecode-2 directory here.
No physical device or personal media is needed.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
MODULE = ROOT / 'mobile-app/modules/avsync/ios'
HERE = Path(__file__).resolve().parent
fixture = Path(sys.argv[1]).resolve()

def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)

devices = json.loads(subprocess.check_output(['xcrun', 'simctl', 'list', 'devices', 'booted', '--json']))
device = next(d['udid'] for runtime, group in devices['devices'].items() if '.iOS-' in runtime for d in group if d['state'] == 'Booted')
sdk = subprocess.check_output(['xcrun', '--sdk', 'iphonesimulator', '--show-sdk-path'], text=True).strip()
with tempfile.TemporaryDirectory(prefix='avsync-debug-recorder-') as temp:
    folder = Path(temp)
    includes = ['-I'+str(MODULE), '-I'+str(ROOT/'core/include'), '-I'+str(ROOT/'third_party'), '-I'+str(ROOT/'third_party/pocketfft')]
    sources = sorted(MODULE.glob('Core_*.cpp'))
    frameworks = [part for name in ['Foundation', 'AVFoundation', 'UIKit', 'CoreMedia', 'CoreVideo', 'CoreGraphics', 'AudioToolbox'] for part in ['-framework', name]]
    run(['xcrun', '--sdk', 'iphonesimulator', 'clang++', '-target', 'arm64-apple-ios15.1-simulator', '-isysroot', sdk, '-std=c++20', '-O2', '-fobjc-arc', '-DAVSYNC_DIAGNOSTIC_CAPTURE=1', *includes, HERE/'live-runner.mm', MODULE/'AVSyncStreamAnalyzer.mm', *sources, *frameworks, '-o', folder/'capture'])
    run(['clang++', '-std=c++20', '-O2', *includes, HERE/'diagnostic-replay.cpp', *sources, '-o', folder/'replay'])
    env = {key:value for key,value in os.environ.items() if not key.startswith('SIMCTL_CHILD_AVSYNC_')}
    env.update(SIMCTL_CHILD_AVSYNC_DEBUG_CAPTURE_DIR=str(folder/'raw'), SIMCTL_CHILD_AVSYNC_TEST_ADVANCE_QR='1', SIMCTL_CHILD_AVSYNC_TEST_QR=(fixture/'qr.json').read_text())
    def capture(environment):
        return json.loads(subprocess.check_output(['xcrun','simctl','spawn',device,str(folder/'capture'),str(fixture/'signal.mov')],env=environment,text=True))
    disabled = capture(env)
    assert all('diagnosticCapture' not in row for row in disabled)
    assert not (folder/'raw').exists(), 'must not record without explicit opt-in'
    enabled = capture({**env, 'SIMCTL_CHILD_AVSYNC_DEBUG_CAPTURE':'1'})
    status = enabled[-1]['diagnosticCapture']
    assert status['complete'] and not status['failed'] and not status['droppedVideo'] and not status['droppedAudio'], status
    directory = Path(status['directory'])
    rows = [json.loads(line) for line in (directory/'index.jsonl').read_text().splitlines()]
    assert rows[-1]['kind']=='end' and rows[-1]['videoBytes']<=512*1024*1024
    times = [row['timeMs'] for row in rows if row['kind']=='video']
    assert 4900<=max(times)-min(times)<=5000.01
    replay = json.loads(subprocess.check_output([str(folder/'replay'),str(directory)],text=True))
    offsets = [row['offset_ms'] for row in replay['events'] if 'offset_ms' in row]
    assert replay['complete'] and replay['traceMismatches']==0 and len(offsets)>=5
    assert all(abs(value-35)<1 for value in offsets), replay
    print('PASS: explicit opt-in only; five-second/512-MiB bound; no debug drops; exact native trace and +35-ms raw replay.')
