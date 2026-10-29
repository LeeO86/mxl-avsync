import test from 'node:test';
import assert from 'node:assert/strict';
import { clockSample, checkClock, clockSummary } from './clock.mjs';
const epoch = Date.parse('2026-10-26T12:00:00Z');
test('HTTPS clock check brackets asymmetric network delay and timestamp resolution', () => {
 const sample = clockSample({ utc_datetime: '2026-10-26T12:00:00.050123+00:00' }, epoch, epoch + 100, 100);
 assert.deepEqual(sample, { lowerMs: -51, upperMs: 51, roundTripMs: 100 });
 const seconds = clockSample({ unixtime: epoch / 1000 }, epoch, epoch + 100, 100);
 assert.equal(seconds.upperMs, 1000);
 assert.throws(() => clockSample({}, epoch, epoch+100, 100));
 assert.throws(() => clockSample({ unixtime: epoch / 1000 }, epoch, epoch+100, 100, '5'));
 assert.throws(() => clockSample({ unixtime: epoch / 1000 }, epoch, epoch+2000, 100));
});
test('clock label expires after a wall-clock change and reports gross errors', () => {
 const sample = { checkedAt: epoch, checkedMono: 0, lowerMs: 3000, upperMs: 3100, roundTripMs: 100 };
 assert.match(clockSummary(sample, epoch+100, 100), /Large clock mismatch/);
 assert.match(clockSummary(sample, epoch+5000, 100), /expired/);
 assert.match(clockSummary(sample, epoch+400000, 400000), /expired/);
});
test('three explicit clock requests combine bounds and handle offline service', async () => {
 let wall = epoch, mono = 0, calls = 0;
 const result = await checkClock({ wallNow: () => wall, monoNow: () => mono, fetchImpl: async () => {
  calls++; wall += 100; mono += 100;
  return { ok: true, headers: { get: () => null }, json: async () => ({ utc_datetime: new Date(wall-50).toISOString() }) };
 } });
 assert.equal(calls, 3); assert.equal(result.samples, 3); assert.equal(result.coarseOnly, true);
 assert.equal(result.lowerMs, -51); assert.equal(result.upperMs, 51);
 await assert.rejects(checkClock({ fetchImpl: async () => { throw new Error('offline'); } }), /offline/);
});
