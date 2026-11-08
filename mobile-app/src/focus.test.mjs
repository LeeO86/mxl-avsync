import test from 'node:test';
import assert from 'node:assert/strict';
import { tapPoint } from './focus.mjs';
import { liveView } from './live.mjs';

test('a tap maps to 0…1 across the camera box, clamped, and needs a laid-out box', () => {
 assert.deepEqual(tapPoint(150, 300, 300, 600), { x: 0.5, y: 0.5 });
 assert.deepEqual(tapPoint(0, 600, 300, 600), { x: 0, y: 1 });
 assert.deepEqual(tapPoint(-5, 650, 300, 600), { x: 0, y: 1 });
 assert.equal(tapPoint(10, 10, 0, 600), null);
 assert.equal(tapPoint(Number.NaN, 10, 300, 600), null);
});

test('while refocusing the report is cleared and the last reading stays greyed (held)', () => {
 const held = { offsetMs: 12, spreadMs: 2, events: 8, atMs: Date.now() - 1000, fresh: true };
 const view = liveView(null, { held });
 assert.equal(view.stage, 'starting');
 assert.equal(view.reading.fresh, false);
 assert.equal(view.reading.offsetMs, 12);
});
