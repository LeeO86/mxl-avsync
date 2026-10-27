"""Bounded native MXL regression: synthetic phone, camera raster and image loss.

Requires the MXL build's mxl-avsync and avsync-phone-fixture. No Docker, router,
clock changes or privileged user are required. Set MXL_LIB_DIR/VCPKG_LIB_DIR
when those shared libraries are not already on the loader's search path.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import socket
import statistics
import subprocess
import tempfile
import time
import urllib.request
import uuid

ROOT = Path(__file__).resolve().parents[2]


def free_port():
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        return listener.getsockname()[1]


def child_environment():
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(('AVSYNC_', 'MXL_', 'NMOS_')) and key != 'WEB_PORT'}
    libraries = [os.environ.get(name, '') for name in ('MXL_LIB_DIR', 'VCPKG_LIB_DIR', 'LD_LIBRARY_PATH')]
    if any(libraries):
        env['LD_LIBRARY_PATH'] = ':'.join(filter(None, libraries))
    return env


def run_rate(binary, fixture, output, rate):
    folder = output / f'{rate}hz'
    folder.mkdir()
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    port = free_port()

    def get(path):
        with opener.open(f'http://127.0.0.1:{port}{path}', timeout=3) as response:
            return json.load(response)

    with tempfile.TemporaryDirectory(prefix='avsync-phone-', dir='/dev/shm') as directory:
        work = Path(directory)
        processes, logs = [], []
        try:
            manifest = folder / 'manifest.json'
            env = child_environment()
            log = (folder / 'publisher.log').open('w')
            logs.append(log)
            publisher = subprocess.Popen(
                [str(fixture), str(work / 'camera'), str(rate), str(manifest),
                 *(str(uuid.uuid4()) for _ in range(3))],
                env=env, stdout=log, stderr=subprocess.STDOUT)
            processes.append(publisher)
            env.update({
                'AVSYNC_ROLE': 'analyzer', 'NMOS_ENABLE': 'false',
                'NMOS_SEED': 'phone-optical-regression', 'MXL_DOMAIN_SCAN_PATH': str(work),
                'MXL_OUTPUT_DOMAIN_DIR': str(work / 'analyzer'),
                'AVSYNC_STATE_DIR': str(work / 'state'), 'WEB_PORT': str(port),
                'AVSYNC_CLOCK_MAX_ERROR_US': '5000', 'AVSYNC_RESULT_ENABLE': 'false',
            })
            log = (folder / 'analyzer.log').open('w')
            logs.append(log)
            analyzer = subprocess.Popen([str(binary), 'serve'], env=env, stdout=log, stderr=subprocess.STDOUT)
            processes.append(analyzer)
            deadline = time.monotonic() + 30
            statuses = []
            while time.monotonic() < deadline:
                if analyzer.poll() is not None:
                    raise RuntimeError(f'analyzer exited {analyzer.returncode}')
                if publisher.poll() is not None:
                    assert publisher.returncode == 0, f'fixture exited {publisher.returncode}'
                    break
                try:
                    statuses.append({'observed_tai_ns': time.clock_gettime_ns(time.CLOCK_TAI),
                                     'status': get('/api/v1/status')})
                except OSError:
                    pass  # Startup may not have opened the HTTP listener yet.
                time.sleep(0.15)
            else:
                raise RuntimeError('fixture exceeded 30 seconds')
            events = get('/api/v1/events?limit=100')['events']
            spec = json.loads(manifest.read_text())
            (folder / 'events.json').write_text(json.dumps(events, indent=2))
            (folder / 'statuses.json').write_text(json.dumps(statuses, indent=2))
            good = [event for event in events
                    if event.get('av_offset_ms') is not None and event.get('grade') != 'invalid']
            assert good, events
            before = [event for event in good if event['video_event_time'] < spec['lost_from_ns']]
            after = [event for event in good if event['video_event_time'] > spec['lost_until_ns'] + 500_000_000]
            # Include invalid events too: no stale video observation may be emitted.
            blank = [event for event in events if spec['lost_from_ns'] + 200_000_000
                     < event.get('video_event_time', 0) < spec['lost_until_ns'] - 200_000_000]
            assert len(before) >= 4 and len(after) >= 4, (rate, 'recovery', events)
            assert not blank, (rate, 'stale video during picture loss', blank)
            for event in good:
                assert event.get('audio_timecode', {}).get('valid'), event
                assert event['audio_timecode']['source_id'] == spec['source_id'], event
                assert event.get('latency_estimated') is True, event
                assert event.get('source_reference') == 'phone-utc-scheduled-output', event
                assert event.get('latency_uncertainty_status') == 'unknown', event
                assert 'optical_capture' in event['flags'] and event.get('uncertainty_ms') is None, event
                assert event.get('audio_latency_ms') is not None and event.get('video_latency_ms') is not None, event
                # One received 50-Hz camera frame plus sub-sample modem tolerance.
                assert abs(event['av_offset_ms'] - 35) <= 20.5, (rate, event)
            values = [event['av_offset_ms'] for event in good]
            result = {'rate': rate, 'valid_pairs': len(good), 'before_loss': len(before),
                      'after_recovery': len(after), 'video_events_inside_blank': len(blank),
                      'offset_min_ms': min(values), 'offset_median_ms': statistics.median(values),
                      'offset_max_ms': max(values), 'all_estimated': True,
                      'source_id': spec['source_id'], 'receiver_clock': statuses[-1]['status']['ptp']}
            print(json.dumps(result), flush=True)
            return result
        except BaseException:
            for log in logs:
                log.flush()
            for name in ('publisher.log', 'analyzer.log'):
                path = folder / name
                if path.exists():
                    print(f'{name}:\n' + '\n'.join(path.read_text().splitlines()[-30:]), flush=True)
            raise
        finally:
            for process in processes:
                if process.poll() is None:
                    process.terminate()
            for process in processes:
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            for log in logs:
                log.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path(os.environ.get('BIN', ROOT / 'build/mxl-avsync')))
    parser.add_argument('--fixture', type=Path)
    parser.add_argument('--out', type=Path, help='Retain logs/results; use a fresh output directory')
    args = parser.parse_args()
    binary = args.binary.resolve()
    fixture = (args.fixture or binary.with_name('avsync-phone-fixture')).resolve()
    for executable in (binary, fixture):
        if not os.access(executable, os.X_OK):
            parser.error(f'missing executable: {executable}; build with MXL enabled')

    def interrupted(signum, _frame):
        raise SystemExit(128 + signum)

    signal.signal(signal.SIGTERM, interrupted)
    with tempfile.TemporaryDirectory(prefix='avsync-phone-results-') as temporary:
        output = args.out.resolve() if args.out else Path(temporary)
        output.mkdir(parents=True, exist_ok=True)
        results = [run_rate(binary, fixture, output, rate) for rate in (1, 2)]
        (output / 'result.json').write_text(json.dumps(results, indent=2))
        print('PASS: 720p60 phone -> tiled 1080p50 MXL, estimated timing, image loss/recovery, 1/2 Hz')


if __name__ == '__main__':
    main()
