"""Container generator with AVSYNC_PATTERN=calibration (docs/calibrator.md, display mode).

Starts mxl-avsync as a generator on a tmpfs MXL domain, checks the calibration pattern with
avsync-calibration-flow (halves swap every 500 ms, beep 100 ms after each swap, a simulated
calibrator gives 0 ms), switches the pattern live through the Signal API and back.
Needs the MXL build. Set MXL_LIB_DIR/VCPKG_LIB_DIR when the loader needs them.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[2]


def free_port():
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        return listener.getsockname()[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', default=str(ROOT / 'build/mxl-avsync'))
    parser.add_argument('--tool', default=None)
    parser.add_argument('--format', default='1080p50')
    args = parser.parse_args()
    tool = args.tool or str(Path(args.binary).resolve().parent / 'avsync-calibration-flow')
    env = {k: v for k, v in os.environ.items() if not k.startswith(('AVSYNC_', 'MXL_', 'NMOS_')) and k != 'WEB_PORT'}
    libraries = [os.environ.get(name, '') for name in ('MXL_LIB_DIR', 'VCPKG_LIB_DIR', 'LD_LIBRARY_PATH')]
    if any(libraries):
        env['LD_LIBRARY_PATH'] = ':'.join(filter(None, libraries))
    port = free_port()
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def call(method, path, body=None):
        data = json.dumps(body).encode() if body is not None else None
        request = urllib.request.Request(f'http://127.0.0.1:{port}{path}', data=data, method=method, headers={'Content-Type': 'application/json'})
        with opener.open(request, timeout=3) as response:
            return json.load(response)

    with tempfile.TemporaryDirectory(prefix='avsync-cal-', dir='/dev/shm') as directory:
        work = Path(directory)
        env.update({'AVSYNC_ROLE': 'generator', 'AVSYNC_PATTERN': 'calibration', 'AVSYNC_VIDEO_FORMAT': args.format,
                    'NMOS_ENABLE': 'false', 'NMOS_SEED': 'calibration-pattern-test', 'MXL_DOMAIN_SCAN_PATH': str(work),
                    'MXL_OUTPUT_DOMAIN_DIR': str(work / 'gen'), 'AVSYNC_STATE_DIR': str(work / 'state'), 'WEB_PORT': str(port),
                    'AVSYNC_CLOCK_MAX_ERROR_US': '5000', 'AVSYNC_RESULT_ENABLE': 'false'})
        (work / 'state').mkdir()
        log = (work / 'generator.log').open('w')
        generator = subprocess.Popen([args.binary, 'serve'], env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 20
            status = {}
            while time.monotonic() < deadline:
                try:
                    status = call('GET', '/api/v1/status')
                    if status.get('media_ready') and status.get('video_flow'):
                        break
                except OSError:
                    pass
                time.sleep(.2)
            assert status.get('video_flow'), f'generator not ready: {status}'
            assert call('GET', '/api/v1/generator')['pattern'] == 'calibration'
            time.sleep(1)
            check = subprocess.run([tool, str(work / 'gen'), status['video_flow'], status['audio_flow'], '5'], env=env,
                                   capture_output=True, text=True, timeout=30)
            print(check.stdout.strip() or check.stderr.strip())
            assert check.returncode == 0, check.stderr
            # Live switch to the test signal: the tool must now fail (no swaps of the halves).
            assert call('PATCH', '/api/v1/generator', {'pattern': 'test'})['ok']
            assert call('GET', '/api/v1/generator')['pattern'] == 'test'
            time.sleep(.5)
            other = subprocess.run([tool, str(work / 'gen'), status['video_flow'], status['audio_flow'], '2'], env=env,
                                   capture_output=True, text=True, timeout=30)
            assert other.returncode != 0, 'test signal must not pass as the calibration pattern'
            try:
                call('PATCH', '/api/v1/generator', {'pattern': 'bogus'})
                raise AssertionError('bogus pattern accepted')
            except urllib.error.HTTPError as error:
                assert error.code == 400
            print(f'PASS calibration pattern ({args.format})')
        finally:
            generator.terminate()
            generator.wait(timeout=15)
            log.close()


if __name__ == '__main__':
    main()
