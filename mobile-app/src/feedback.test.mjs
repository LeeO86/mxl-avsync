import test from 'node:test';
import assert from 'node:assert/strict';
import { signalFeedback, latencyValues, latencyHint } from './feedback.mjs';
const base = { timeMs: 6000, picture: { found: true, codeDecoded: false }, chirps: [], events: [] };
test('independent feedback progresses from absent to detected to confirmed', () => {
 assert.equal(signalFeedback({}, 0).audio.tone, 'gray');
 assert.equal(signalFeedback(base, 0).picture.tone, 'orange');
 const report = { ...base, decodedSeq: 101, picture: { found: true, codeDecoded: true }, chirps: [{ timeMs: 5800, id: 1, quality: 'degraded' }] };
 assert.equal(signalFeedback(report, 0).audio.tone, 'orange');
 assert.equal(signalFeedback(report, 0).picture.tone, 'green');
 report.events = [{ seq: 101, videoTimeMs: 5780, audioTimeMs: 5800, offset_ms: 20, valid: true, reason: '' }];
 assert.equal(signalFeedback(report, 0).audio.tone, 'orange'); // Four IDs are not an audio timecode.
 report.audioTimecode = { valid: true, crcOk: true, receivedTimeMs: 5800, eventSeq: 101 };
 assert.equal(signalFeedback(report, 0).audio.tone, 'green');
 report.audioTimecode.crcOk = false;
 assert.equal(signalFeedback(report, 0).audio.tone, 'orange');
 delete report.audioTimecode;
 report.events[0].valid = false;
 assert.equal(signalFeedback(report, 0).audio.tone, 'orange');
 report.events[0].valid = true; report.events[0].seq = 102;
 assert.equal(signalFeedback(report, 0).audio.tone, 'orange');
});
test('buffered historical chirps, paused capture and stale reports cannot stay green', () => {
 const report = { ...base, picture: { found: true, codeDecoded: true }, chirps: [{ timeMs: 2000, id: 1, quality: 'good' }] };
 assert.equal(signalFeedback(report, 0).audio.tone, 'gray');
 assert.equal(signalFeedback(report, 2000).picture.tone, 'gray');
 assert.equal(signalFeedback(report, 0, false).picture.tone, 'gray');
});
test('latency shows finite estimates including negative clock mismatches but expires', () => {
 const report = { latency: { state: 'estimated', video_ms: -50, audio_ms: 120 } };
 assert.deepEqual(latencyValues(report, 0), { video: -50, audio: 120 });
 assert.deepEqual(latencyValues(report, 2000), { video: null, audio: null });
 assert.deepEqual(latencyValues({ latency: { state: 'file_signal', video_ms: 50 } }, 0), { video: null, audio: null });
});

test('latency distinguishes a file signal, expired QR and absent capture clock', () => {
 assert.match(latencyHint('file_signal'), /File test/);
 assert.match(latencyHint('qr_expired'), /expired/);
 assert.match(latencyHint('waiting_for_device_clock'), /clock mapping unavailable/);
 assert.match(latencyHint({state:'estimated',videoState:'qr_expired',audioState:'estimated'}), /expired/);
 assert.match(latencyHint({state:'estimated',videoState:'qr_event_mismatch'}), /do not match/);
});

test('compact timecode needs no sequence field and remains green through its declared packet lease', () => {
 const report={timeMs:1500,audioTimecode:{valid:true,crcOk:true,receivedTimeMs:100,validUntilMs:1700}};
 assert.equal(signalFeedback(report,0,true).audio.tone,'green');
 assert.equal(signalFeedback(report,0,true).audio.label,'Audio timecode decoded');
 report.timeMs=1701; assert.equal(signalFeedback(report,0,true).audio.tone,'gray');
});
