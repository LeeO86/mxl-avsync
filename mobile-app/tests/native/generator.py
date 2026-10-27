"""Verify the actual native generator renderer and waveform in an iOS simulator."""
import json
import subprocess
import tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
module = ROOT / 'mobile-app/modules/avsync/ios'
devices = json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json']))
device = next((d['udid'] for runtime, group in devices['devices'].items() if '.iOS-' in runtime for d in group if d['state']=='Booted'), None)
if not device: raise SystemExit('Boot an arm64 iOS simulator first.')
sdk = subprocess.check_output(['xcrun','--sdk','iphonesimulator','--show-sdk-path'],text=True).strip()
with tempfile.TemporaryDirectory(prefix='avsync-phone-generator-') as directory:
    binary=Path(directory)/'generator'
    subprocess.run(['xcrun','--sdk','iphonesimulator','clang++','-std=c++20','-O2','-fobjc-arc','-target','arm64-apple-ios15.1-simulator','-isysroot',sdk,'-I'+str(module),'-I'+str(ROOT/'core/include'),'-I'+str(ROOT/'third_party/pocketfft'),str(Path(__file__).with_name('generator-runner.mm')),str(module/'AVSyncGenerator.mm'),str(module/'AVSyncQRCodeReader.mm'),*[str(p) for p in sorted(module.glob('Core_*.cpp'))],*[a for f in ['Foundation','UIKit','AVFoundation','CoreMedia','CoreVideo','CoreGraphics','Vision'] for a in ['-framework',f]],'-o',str(binary)],check=True)
    subprocess.run(['xcrun','simctl','spawn',device,str(binary)],check=True)
