import test from 'node:test';
import assert from 'node:assert/strict';
import {captureRate,captureRateLabel,captureStatus} from './capture.mjs';
test('120 fps is the initial comparison mode; maximum and explicit caps are retained',()=>{
 assert.equal(captureRate({}),120);assert.equal(captureRate({captureFps:0}),0);
 for(const cap of [60,120,240])assert.equal(captureRate({captureFps:cap}),cap);
 assert.equal(captureRate({captureFps:NaN}),120);assert.equal(captureRate({captureFps:90}),120);
 assert.equal(captureRateLabel(0),'Maximum');
});
test('capture UI shows configured camera rate and recent delivered rate separately',()=>{
 assert.equal(captureStatus({configuredFps:120,fps:99,captureStats:{deliveredFps:119.7}}),'120 fps camera · 120 fps analysed');
 assert.equal(captureStatus({configuredFps:240,fps:180}),'240 fps camera · 180 fps analysed');
});
