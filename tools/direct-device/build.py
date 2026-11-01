#!/usr/bin/env python3
"""Build the native Mac test source locally; no EAS or media transport.
Usage: python3 tools/direct-device/build.py [output-directory]
Optional AVSYNC_SIGN_IDENTITY selects a local codesigning identity (default ad hoc).
"""
import os, pathlib, plistlib, shutil, subprocess, sys
root=pathlib.Path(__file__).resolve().parents[2]
module=root/'mobile-app/modules/avsync/ios'
out=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else '/tmp/avsync-direct-device').resolve()
app=out/'AVsync Direct Test.app';mac=app/'Contents/MacOS';resources=app/'Contents/Resources/AvsyncResources.bundle'
mac.mkdir(parents=True,exist_ok=True);resources.mkdir(parents=True,exist_ok=True)
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
arch=subprocess.check_output(['uname','-m'],text=True).strip();target=f'{arch}-apple-macos14.0'
objects=[]
for source in [module/'AVSyncGenerator.mm',module/'Mobile_text.mm',module/'Mobile_picture.cpp',*sorted(module.glob('Core_*.cpp'))]:
    obj=out/(source.name+'.o');objects.append(str(obj))
    subprocess.run(['xcrun','clang++','-std=c++20','-O2',*(['-fobjc-arc'] if source.suffix=='.mm' else []),'-target',target,'-isysroot',sdk,'-I'+str(module),'-I'+str(root/'core/include'),'-I'+str(root/'third_party/pocketfft'),'-I'+str(root/'third_party/nayuki'),'-c',str(source),'-o',str(obj)],check=True)
subprocess.run(['xcrun','swiftc','-O','-target',target,'-sdk',sdk,'-module-cache-path',str(out/'swift-cache'),'-import-objc-header',str(pathlib.Path(__file__).with_name('Bridge.h')),'-I'+str(module),str(pathlib.Path(__file__).with_name('main.swift')),str(module/'SntpPacket.swift'),str(module/'PhoneClock.swift'),str(module/'PeerClockPacket.swift'),str(module/'PeerClock.swift'),*objects,*[a for f in ['AppKit','QuartzCore','AVFoundation','CoreMedia','CoreGraphics','CoreText','Network','Vision'] for a in ['-framework',f]],'-lc++','-o',str(mac/'AVsyncDirectTest')],check=True)
for name in ['DejaVuSans.ttf','icon.png','LICENSE']:
    source=module/'Resources'/name
    if source.exists():shutil.copy(source,resources/name)
(resources/'Info.plist').write_bytes(plistlib.dumps({'CFBundleIdentifier':'io.leeo86.avsync.resources','CFBundlePackageType':'BNDL'}))
(app/'Contents/Info.plist').write_bytes(plistlib.dumps({'CFBundleIdentifier':'io.leeo86.avsync.direct-test','CFBundleExecutable':'AVsyncDirectTest','CFBundleName':'AVsync Direct Test','CFBundlePackageType':'APPL','CFBundleVersion':'1','LSMinimumSystemVersion':'14.0','NSHighResolutionCapable':True,'NSLocalNetworkUsageDescription':'Compare time with a nearby AVsync receiver for a direct picture and sound test.','NSBonjourServices':['_avsync-peer._udp']}))
subprocess.run(['codesign','--force','--sign',os.environ.get('AVSYNC_SIGN_IDENTITY','-'),str(app)],check=True)
subprocess.run([str(mac/'AVsyncDirectTest'),'--self-test',str(out/'preview.png')],check=True)
print(app)
