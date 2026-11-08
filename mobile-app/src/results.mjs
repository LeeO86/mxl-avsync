import { offsetText } from './live.mjs';

// Saved results grouped by sender, with a nickname and one reference result per sender.
// Session state: senders = { [sourceId]: { nickname?: string, referenceId?: string } }.
export const RESULT_LIMIT = 100;

// The key of a result's sender; null for results without a sender ID (0 means no timecode ID).
export function senderKey(sourceId) {
 return Number.isInteger(sourceId) && sourceId > 0 ? String(sourceId) : null;
}

// Short form of the sender ID. The analyser does not know the generator's title (it is only
// drawn in the picture, not in the QR), so the default name is this short ID.
export function shortId(result) {
 if (typeof result?.testId === 'string' && result.testId) return result.testId.length > 10 ? `${result.testId.slice(0, 10)}…` : result.testId;
 const key = senderKey(result?.sourceId);
 return key ? `ID ${(Number(key) >>> 0).toString(16).padStart(8, '0').slice(0, 4)}…` : null;
}

export function senderName(senders, key, sample) {
 const nickname = senders?.[key]?.nickname;
 return typeof nickname === 'string' && nickname.trim() ? nickname.trim() : shortId(sample) || 'Unknown sender';
}

export function referenceFor(senders, key, results) {
 const id = key ? senders?.[key]?.referenceId : null;
 return id ? results.find(r => r.id === id && senderKey(r.sourceId) === key) || null : null;
}

const diff = (a, b) => Number.isFinite(a) && Number.isFinite(b) ? a - b : null;

// Latency Δ only when both results had latency, i.e. both had a clock reference.
export function resultDelta(result, reference) {
 if (!result || !reference) return null;
 return { offsetMs: diff(result.offsetMs, reference.offsetMs), videoLatencyMs: diff(result.videoLatencyMs, reference.videoLatencyMs), audioLatencyMs: diff(result.audioLatencyMs, reference.audioLatencyMs) };
}

// "check" when the camera mode or the calibration offset differs from the reference's.
// A missing camera mode on either side (older results) is not compared.
export function needsCheck(reference, { captureContext = null, calibrationMs = null } = {}) {
 if (!reference) return false;
 const was = reference.captureContext?.cameraMode, now = captureContext?.cameraMode;
 if (was !== undefined && now !== undefined && was !== now) return true;
 return (reference.calibrationMs ?? null) !== (calibrationMs ?? null);
}

// Groups for the Results tab: senders by newest result, "Unknown sender" last.
// Inside a group the reference comes first, then the others newest first.
export function groupResults(results, senders) {
 const groups = new Map();
 for (const result of results) {
  const key = senderKey(result.sourceId);
  if (!groups.has(key)) groups.set(key, []);
  groups.get(key).push(result);
 }
 const list = [...groups].map(([key, items]) => {
  const reference = referenceFor(senders, key, items);
  const sorted = [...items].sort((a, b) => b.at - a.at);
  const ordered = reference ? [reference, ...sorted.filter(r => r !== reference)] : sorted;
  return {
   key, name: key ? senderName(senders, key, sorted[0]) : 'Unknown sender', id: key ? shortId(sorted[0]) : null,
   latestAt: sorted[0].at, reference,
   items: ordered.map(result => ({
    result, isReference: result === reference,
    delta: reference && result !== reference ? resultDelta(result, reference) : null,
    check: reference && result !== reference ? needsCheck(reference, result) : false,
   })),
  };
 });
 return list.sort((a, b) => (a.key === null) - (b.key === null) || b.latestAt - a.latestAt);
}

// The Δ line on Measure for the sender currently heard, or null without a reference.
export function liveDelta(reading, { senders, results, captureContext = null, calibrationMs = null }) {
 const key = senderKey(reading?.sourceId);
 const reference = referenceFor(senders, key, results || []);
 if (!reference) return null;
 return { name: senderName(senders, key, reference), ...resultDelta(reading, reference), check: needsCheck(reference, { captureContext, calibrationMs }) };
}

export function deltaText(ms) {
 return Number.isFinite(ms) ? `Δ ${offsetText(ms)}` : '';
}

// ★ toggles: the result becomes its sender's reference, or stops being it.
export function toggleReference(senders, result) {
 const key = senderKey(result?.sourceId);
 if (!key) return senders;
 const entry = { ...senders?.[key] };
 if (entry.referenceId === result.id) delete entry.referenceId; else entry.referenceId = result.id;
 return { ...senders, [key]: entry };
}

// An empty nickname goes back to the default name.
export function setNickname(senders, key, nickname) {
 if (!key) return senders;
 const entry = { ...senders?.[key] };
 const value = typeof nickname === 'string' ? nickname.replace(/\s+/gu, ' ').trim().slice(0, 40) : '';
 if (value) entry.nickname = value; else delete entry.nickname;
 return { ...senders, [key]: entry };
}

// New results first, at most RESULT_LIMIT; a reference is never dropped by the limit.
export function addResult(results, item, senders) {
 const refs = new Set(Object.values(senders || {}).map(s => s?.referenceId).filter(Boolean));
 const all = [item, ...(results || [])];
 let kept = 0;
 return all.filter(r => refs.has(r.id) || kept++ < RESULT_LIMIT);
}

// Session state from saveState; older states have no senders map.
export function loadSessionState(json) {
 let state = null;
 try { state = JSON.parse(json || 'null'); } catch { state = null; }
 if (!state || typeof state !== 'object' || Array.isArray(state)) state = {};
 return {
  ...state,
  calibration: state.calibration ?? null,
  results: Array.isArray(state.results) ? state.results : [],
  senders: state.senders && typeof state.senders === 'object' && !Array.isArray(state.senders) ? state.senders : {},
 };
}
