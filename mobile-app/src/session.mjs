// Live reading, saved results, and the calibration the operator keeps.
// Clearing the live reading does not touch the list. Newest result is first.

import { shownOffset } from './measure.mjs';

export function createSession() {
  return { live: null, calibration: null, results: [] };
}

export function clearLive(session) {
  return { ...session, live: null };
}

export function clearCalibration(session) {
  return { ...session, calibration: null };
}

export function clearResults(session) {
  return { ...session, results: [] };
}

export function saveCalibration(session, calibration) {
  return { ...session, calibration };
}

export function setLive(session, reading) {
  if (!reading || reading.offset_ms === null || reading.offset_ms === undefined || Number.isNaN(reading.offset_ms)) {
    return session;
  }
  return {
    ...session,
    live: {
      offset_ms: shownOffset(reading.offset_ms, session.calibration),
      quality: reading.quality || '—',
      uncertaintyMs: reading.uncertainty_ms ?? null,
    },
  };
}

export function completeMeasurement(session, reading, unixMs) {
  if (!reading || reading.offset_ms === null || reading.offset_ms === undefined || Number.isNaN(reading.offset_ms)) {
    return session;
  }
  const item = {
    id: `${unixMs}-${session.results.length}`,
    at: unixMs,
    offset_ms: shownOffset(reading.offset_ms, session.calibration),
    quality: reading.quality || '—',
    uncertaintyMs: reading.uncertainty_ms ?? null,
  };
  return { ...session, live: item, results: [item, ...session.results] };
}
