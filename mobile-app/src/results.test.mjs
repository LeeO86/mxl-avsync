import test from 'node:test';
import assert from 'node:assert/strict';
import { addResult, deltaText, groupResults, liveDelta, loadSessionState, needsCheck, resultDelta, senderKey, setNickname, shortId, toggleReference, RESULT_LIMIT } from './results.mjs';

const cam = { cameraMode: '1920x1080@240' };
const r = (id, at, extra) => ({ id, at, offsetMs: 3.1, videoLatencyMs: 41, audioLatencyMs: 38, calibrationMs: 2, captureContext: cam, sourceId: 11, testId: 'phone-3fa9c0d1e2', ...extra });

test('sender keys and default names', () => {
 assert.equal(senderKey(11), '11');
 assert.equal(senderKey(0), null);
 assert.equal(senderKey(null), null);
 assert.equal(shortId({ testId: 'phone-3fa9c0d1e2', sourceId: 11 }), 'phone-3fa9…');
 assert.equal(shortId({ testId: 'ctr', sourceId: 11 }), 'ctr');
 assert.equal(shortId({ sourceId: 0x3fa9c0d1 }), 'ID 3fa9…');
 assert.equal(shortId({}), null);
});

test('results group by sender, unknown last, reference first with Δ', () => {
 const results = [r('4', 400, { sourceId: null, testId: null }), r('3', 300, { offsetMs: 21.5, videoLatencyMs: 103, audioLatencyMs: null }), r('2', 200, { sourceId: 22, testId: 'ctr-91c2aa77' }), r('1', 100)];
 const senders = { 11: { nickname: 'Studio iPhone', referenceId: '1' } };
 const groups = groupResults(results, senders);
 assert.deepEqual(groups.map(g => [g.key, g.name, g.id]), [['11', 'Studio iPhone', 'phone-3fa9…'], ['22', 'ctr-91c2aa…', 'ctr-91c2aa…'], [null, 'Unknown sender', null]]);
 const studio = groups[0].items;
 assert.deepEqual(studio.map(i => [i.result.id, i.isReference]), [['1', true], ['3', false]]);
 assert.equal(studio[0].delta, null);
 assert.ok(Math.abs(studio[1].delta.offsetMs - 18.4) < 1e-9);
 assert.equal(studio[1].delta.videoLatencyMs, 62);
 assert.equal(studio[1].delta.audioLatencyMs, null); // no clock on one side: no latency Δ
 assert.equal(groups[1].reference, null);
});

test('a reference from another sender or a missing result is ignored', () => {
 const groups = groupResults([r('1', 100), r('2', 200, { sourceId: 22 })], { 11: { referenceId: '2' }, 22: { referenceId: 'gone' } });
 assert.ok(groups.every(g => g.reference === null));
});

test('check marker when camera mode or calibration differs', () => {
 const ref = r('1', 100);
 assert.equal(needsCheck(ref, { captureContext: cam, calibrationMs: 2 }), false);
 assert.equal(needsCheck(ref, { captureContext: { cameraMode: '1280x720@120' }, calibrationMs: 2 }), true);
 assert.equal(needsCheck(ref, { captureContext: cam, calibrationMs: null }), true);
 assert.equal(needsCheck({ ...ref, captureContext: undefined }, { captureContext: cam, calibrationMs: 2 }), false);
 const groups = groupResults([r('2', 200, { calibrationMs: 5 }), ref], { 11: { referenceId: '1' } });
 assert.equal(groups[0].items[1].check, true);
});

test('live Δ for the sender heard, with its nickname', () => {
 const results = [r('1', 100)];
 const senders = { 11: { nickname: 'Studio iPhone', referenceId: '1' } };
 const d = liveDelta({ sourceId: 11, offsetMs: 21.5, videoLatencyMs: null, audioLatencyMs: 40 }, { senders, results, captureContext: cam, calibrationMs: 2 });
 assert.equal(d.name, 'Studio iPhone');
 assert.equal(deltaText(d.offsetMs), 'Δ +18.4 ms');
 assert.equal(d.videoLatencyMs, null);
 assert.equal(d.audioLatencyMs, 2);
 assert.equal(d.check, false);
 assert.equal(liveDelta({ sourceId: 22, offsetMs: 1 }, { senders, results }), null);
 assert.equal(liveDelta(null, { senders, results }), null);
 assert.equal(deltaText(-2), 'Δ −2.0 ms');
 assert.equal(deltaText(null), '');
});

test('the sender title (AVS4 QR name) is the default nickname; a set nickname wins', () => {
 const results = [r('3', 300, { title: 'Studio B' }), r('2', 200), r('1', 100, { title: 'Studio A' })];
 let groups = groupResults(results, {});
 assert.deepEqual([groups[0].name, groups[0].title, groups[0].id], ['Studio B', 'Studio B', 'phone-3fa9…']); // newest title
 groups = groupResults(results, { 11: { nickname: 'Desk' } });
 assert.equal(groups[0].name, 'Desk');
 assert.equal(groupResults([r('1', 100)], {})[0].name, 'phone-3fa9…'); // no title: short ID
 const senders = { 11: { referenceId: '1' } };
 assert.equal(liveDelta({ sourceId: 11, offsetMs: 1, title: 'Studio C' }, { senders, results }).name, 'Studio C');
 assert.equal(liveDelta({ sourceId: 11, offsetMs: 1 }, { senders, results }).name, 'Studio A'); // the reference's title
});

test('check marker when the sender calibration differs', () => {
 const ref = r('1', 100, { senderCalibrationMs: -38 });
 assert.equal(needsCheck(ref, { captureContext: cam, calibrationMs: 2, senderCalibrationMs: -38 }), false);
 assert.equal(needsCheck(ref, { captureContext: cam, calibrationMs: 2, senderCalibrationMs: null }), true);
 assert.equal(needsCheck(r('1', 100), { captureContext: cam, calibrationMs: 2 }), false); // older result without the field
});

test('reference toggles, nickname sets and resets', () => {
 let senders = toggleReference({}, r('1', 100));
 assert.equal(senders[11].referenceId, '1');
 senders = toggleReference(senders, r('2', 200));
 assert.equal(senders[11].referenceId, '2'); // one reference per sender
 senders = toggleReference(senders, r('2', 200));
 assert.equal(senders[11].referenceId, undefined);
 assert.deepEqual(toggleReference(senders, r('3', 300, { sourceId: null })), senders);
 senders = setNickname(senders, '11', '  Studio   iPhone ');
 assert.equal(senders[11].nickname, 'Studio iPhone');
 senders = setNickname(senders, '11', ' ');
 assert.equal(senders[11].nickname, undefined);
 assert.equal(resultDelta(r('1', 1), null), null);
});

test('the result limit keeps references', () => {
 const old = Array.from({ length: RESULT_LIMIT }, (_, i) => r(String(i), 1000 - i));
 const kept = addResult(old, r('new', 2000), { 11: { referenceId: String(RESULT_LIMIT - 1) } });
 assert.equal(kept.length, RESULT_LIMIT + 1);
 assert.equal(kept[0].id, 'new');
 assert.equal(kept.at(-1).id, String(RESULT_LIMIT - 1));
 assert.equal(addResult(old, r('new', 2000), {}).length, RESULT_LIMIT);
});

test('old session state loads with an empty senders map', () => {
 const old = loadSessionState(JSON.stringify({ calibration: { offset_ms: 1 }, results: [r('1', 1)], keepScreenAwake: false }));
 assert.deepEqual(old.senders, {});
 assert.equal(old.results.length, 1);
 assert.equal(old.keepScreenAwake, false);
 assert.deepEqual(loadSessionState('broken{'), { calibration: null, results: [], senders: {} });
 assert.deepEqual(loadSessionState(null), { calibration: null, results: [], senders: {} });
 assert.deepEqual(loadSessionState('{"results":null,"senders":[]}').senders, {});
});
