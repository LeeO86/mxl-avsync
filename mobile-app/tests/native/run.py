"""Run the real iOS bridge (AVSyncPhoneAnalyzer, Vision QR, CoreMedia buffers) on a
simulated AVS3 camera capture. Requires Xcode and a booted arm64 iOS simulator.
"""
import json
import math
from pathlib import Path
import subprocess
import tempfile
from urllib.parse import unquote, urlparse

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
module = ROOT / 'mobile-app/modules/avsync/ios'

devices = json.loads(subprocess.check_output(['xcrun', 'simctl', 'list', 'devices', 'booted', '--json']))
device = next((d['udid'] for runtime, group in devices['devices'].items() if '.iOS-' in runtime for d in group if d['state'] == 'Booted'), None)
if not device:
    raise SystemExit('Boot an iOS simulator first.')
sdk = subprocess.check_output(['xcrun', '--sdk', 'iphonesimulator', '--show-sdk-path'], text=True).strip()

with tempfile.TemporaryDirectory(prefix='avsync-native-') as temp:
    binary = Path(temp) / 'live-runner'
    frameworks = [a for f in ['Foundation', 'UIKit', 'AVFoundation', 'CoreMedia', 'CoreVideo', 'CoreGraphics', 'CoreText', 'Vision'] for a in ['-framework', f]]
    subprocess.run(['xcrun', '--sdk', 'iphonesimulator', 'clang++', '-target', 'arm64-apple-ios15.1-simulator', '-isysroot', sdk,
                    '-std=c++20', '-O2', '-fobjc-arc', f'-I{module}', f'-I{ROOT}/core/include', f'-I{ROOT}/third_party/pocketfft', f'-I{ROOT}/third_party/nayuki',
                    HERE / 'live-runner.mm', module / 'AVSyncPhoneAnalyzer.mm', module / 'AVSyncQRCodeReader.mm', module / 'Mobile_text.mm',
                    module / 'Mobile_picture.cpp', *sorted(module.glob('Core_*.cpp')), *frameworks, '-o', binary], check=True)

    def spawn(*args):
        return json.loads(subprocess.check_output(['xcrun', 'simctl', 'spawn', device, str(binary), *args], text=True))

    report = spawn()
    reading = report.get('reading')
    assert report['stage'] == 'measuring' and reading, report
    assert abs(reading['offsetMs'] - 20) < 1 and reading['spreadMs'] < 2, reading
    assert abs(reading['videoLatencyMs'] - 30) < 1 and abs(reading['audioLatencyMs'] - 50) < .5, reading
    assert reading['events'] >= 3, reading

    take = spawn('calibration')
    assert take['kind'] == 'calibration', take
    # The take starts at the first pushed camera frame: the first 239.48 fps frame from 3.1 ms once the 30 ms display delay has passed.
    camera = 1000 / 239.48
    first = 3.1 + math.ceil((30 - 3.1) / camera) * camera
    assert len(take['frames']) > 100 and abs(take['spikeTimeMs'] - (2300 - first)) < 2, {k: take[k] for k in take if k != 'frames'}
    assert all(Path(unquote(urlparse(frame['uri']).path)).is_file() for frame in take['frames'])
    assert Path(unquote(urlparse(take['uri']).path)).is_file() and len(take['waveform']) >= 200
    print(f"PASS: native AVS3 live: AV {reading['offsetMs']:+.2f} ms (+20), video {reading['videoLatencyMs']:.2f} ms (30), "
          f"audio {reading['audioLatencyMs']:.2f} ms (50), spread {reading['spreadMs']:.2f} ms; calibration take {len(take['frames'])} frames")
