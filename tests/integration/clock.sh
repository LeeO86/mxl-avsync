#!/usr/bin/env bash
# Clock source/metrics and once-only fallback logging through a real service.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export BIN="${BIN:-$ROOT/build/mxl-avsync}"
python3 - <<'PY'
import contextlib, json, os, socket, struct, subprocess, tempfile, threading, time, urllib.request
from pathlib import Path

opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
def request(port, path):
    with opener.open(f'http://127.0.0.1:{port}{path}', timeout=2) as reply:
        data = reply.read().decode()
        return json.loads(data) if path != '/metrics' else data

def wait(port, predicate):
    for _ in range(100):
        try:
            value = request(port, '/api/v1/status')['ptp']
            if predicate(value): return value
        except OSError: pass
        time.sleep(.05)
    raise AssertionError('clock status never reached expected state')

with tempfile.TemporaryDirectory(prefix='avsync-clock-api-') as directory:
    root = Path(directory)
    @contextlib.contextmanager
    def service(name, **clock):
        listener = socket.socket()
        listener.bind(('127.0.0.1', 0))
        port = listener.getsockname()[1]
        listener.close()
        env = {k: v for k, v in os.environ.items() if not k.startswith(('AVSYNC_', 'NMOS_', 'MXL_', 'WEB_'))}
        env.update(AVSYNC_ROLE='analyzer', NMOS_ENABLE='false', WEB_PORT=str(port), NMOS_PORT=str(port+1),
                   AVSYNC_STATE_DIR=str(root/name), MXL_DOMAIN_SCAN_PATH=str(root/'mxl'),
                   MXL_OUTPUT_DOMAIN_DIR=str(root/name/'output'), MXL_CLEANUP_ON_EXIT='true', **clock)
        with (root/(name+'.log')).open('w') as log:
            child = subprocess.Popen([os.environ['BIN'], 'serve'], env=env, stdout=log, stderr=log)
            try: yield port
            finally:
                child.terminate()
                child.wait(timeout=10)

    missing = str(root/'missing-socket')
    with service('fallback', AVSYNC_PTP_SOCKET=missing) as port:
        value = wait(port, lambda v: v['sync_source'] == 'kernel' and 'ptp4l unavailable' in v['reason'])
        time.sleep(2.2) # several failed polls; warn only once per process
        assert request(port, '/statusz')['ptp']['sync_source'] == 'kernel'
        assert request(port, '/api/v1/config')['AVSYNC_PTP_SOCKET'] == missing
        metrics = request(port, '/metrics')
        assert 'mxl_avsync_ptp_gm_present 0' in metrics
        assert 'mxl_avsync_ptp_master_offset_seconds 0' in metrics
    assert (root/'fallback.log').read_text().count('ptp_socket_fallback') == 1

    with service('override', AVSYNC_PTP_SOCKET=missing, AVSYNC_PTP_STATUS_URL='http://127.0.0.1:1/status') as port:
        value = wait(port, lambda v: v['sync_source'] == 'url')
        assert value['sync_state'] == 'unknown'
    assert 'ptp_socket_fallback' not in (root/'override.log').read_text()

    fake = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    path = str(root/'daemon')
    fake.bind(path)
    fake.settimeout(.1)
    stop = threading.Event()
    state = {'gm': True}
    def daemon():
        while not stop.is_set():
            try: data, address = fake.recvfrom(2048)
            except TimeoutError: continue
            assert data[46] == 0, 'client sent a write action'
            management_id = struct.unpack_from('!H', data, 52)[0]
            if management_id == 0xc000:
                payload = struct.pack('!qqiiH', -123, 0, 0, 0, 0) + bytes(12)
                payload += struct.pack('!I8s', int(state['gm']), bytes.fromhex('0011223344556677'))
            else:
                assert management_id == 0x2004
                payload = bytes.fromhex('00112233445566770001') + bytes([9]) + bytes(15)
            reply = bytearray(data[:54] + payload)
            reply[34:44] = data[20:30]
            reply[20:30] = bytes.fromhex('00112233445566770001')
            reply[46] = 2
            fake.sendto(reply, address)
    worker = threading.Thread(target=daemon)
    worker.start()
    try:
        with service('socket', AVSYNC_PTP_SOCKET=path) as port:
            value = wait(port, lambda v: v['sync_source'] == 'ptp4l')
            assert value['sync_state'] == 'locked' and value['accuracy_ns'] == '123', value
            assert value['master_offset_ns'] == -123 and value['gm_identity'] == '0011223344556677'
            assert value['gm_present'] and value['port_state'] == 'SLAVE'
            metrics = request(port, '/metrics')
            assert 'mxl_avsync_clock_source{sync_source="ptp4l"} 1' in metrics
            assert 'mxl_avsync_ptp_gm_present 1' in metrics
            offset = next(line.split()[-1] for line in metrics.splitlines()
                          if line.startswith('mxl_avsync_ptp_master_offset_seconds '))
            assert abs(float(offset) + 123e-9) < 1e-15
            state['gm'] = False
            value = wait(port, lambda v: v['sync_source'] == 'ptp4l' and not v['gm_present'])
            assert value['sync_state'] == 'unlocked'
        assert 'ptp_socket_fallback' not in (root/'socket.log').read_text()
    finally:
        stop.set()
        worker.join(timeout=2)
        fake.close()
print('clock.sh ok: URL precedence, UDS state/metrics, kernel fallback, once-only warning')
PY
