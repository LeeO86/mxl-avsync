// JS face of the Expo native module. On a dev build the native side replaces this
// file's measurement functions with avsync-core (PhoneSession). Here the module
// exports the contract the UI calls and a local store so the screens render
// without a device.
import { resultDocument } from '../../src/display.mjs';

const listeners = new Set();
let latest = {
  calibration: 'missing',
  grade: 'invalid',
  offset_ms: null,
  sentence: 'no measurement',
  quality: 'invalid',
  test_id: 'PHONE',
  events: [],
};

function emit() {
  for (const listener of listeners) listener({ ...latest });
}

export function subscribe(listener) {
  listeners.add(listener);
  listener({ ...latest });
  return () => listeners.delete(listener);
}

export function applyNativeReport(report) {
  latest = { ...latest, ...report };
  emit();
}

export function exportResult() {
  return resultDocument(latest);
}

export default { subscribe, applyNativeReport, exportResult };
