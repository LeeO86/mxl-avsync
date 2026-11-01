import test from 'node:test';
import assert from 'node:assert/strict';
import {captureRate,captureRateLabel,captureReportAge,captureStatus,captureDiagnostics} from './capture.mjs';
import {receiveReading,readingPresentation,savedReading} from './reading.mjs';
test('120 fps is the initial comparison mode; maximum and explicit caps are retained',()=>{
 assert.equal(captureRate({}),120);assert.equal(captureRate({captureFps:0}),0);
 for(const cap of [60,120,240])assert.equal(captureRate({captureFps:cap}),cap);
 assert.equal(captureRate({captureFps:NaN}),120);assert.equal(captureRate({captureFps:90}),120);
 assert.equal(captureRateLabel(0),'Maximum');
});
test('capture UI shows configured camera rate and recent delivered rate separately',()=>{
 assert.equal(captureStatus({configuredFps:120,fps:99,captureStats:{deliveredFps:119.7}}),'120 fps camera · 120 fps delivered');
 assert.equal(captureStatus({configuredFps:240,fps:180}),'240 fps camera · 180 fps delivered');
});

test('capture gaps warn without adding overlapping drops or confusing worker time with capture time',()=>{
 const report={configuredFps:120,analysisMode:'async-snapshot-v1',analysisDelayMs:50,
  events:[{seq:6,valid:true,videoTimeMs:500},{seq:7,valid:true,videoTimeMs:1000}],
  captureStats:{deliveredFps:98,windowMs:6000,maxFrameGapMs:41.7,p95FrameGapMs:8.4,dropCallbacks:3,estimatedMissingFramesFromPts:4,audioProcessingMaxMs:1,pollProcessingMaxMs:50,snapshotProcessingMaxMs:2,
   eventFrameGaps:[{seq:7,eventTimeMs:1000,frameGapMs:41.7}]}};
 const value=captureDiagnostics(report,100,true);
 assert.match(value.warning,/Capture gaps/);
 assert.ok(value.lines.some(line=>line.includes('#7')&&line.includes('41.7 ms')));
 assert.ok(value.lines.some(line=>line.includes('3')&&line.includes('4')&&line.includes('overlap')));
 assert.ok(value.lines.some(line=>line.includes('Snapshot copy')&&line.includes('analysis max 50.0 ms')));
 assert.equal(captureDiagnostics(report,2000,true).warning,null);
 assert.equal(captureDiagnostics(report,0,false).warning,null);
 assert.match(captureDiagnostics(report,2000,true).lines[0],/Last report/);
 assert.equal(captureDiagnostics({},0,true).warning,null);
 assert.ok(!captureDiagnostics({...report,events:[{seq:8,valid:true,videoTimeMs:1500}]},0,true).lines.some(line=>line.includes('capture bracket')));
});

test('display and Save both count worker delay when a near-boundary reading becomes held',()=>{
 const report={kind:'reading',offset_ms:0,eventCount:3,timeMs:2000,analysisDelayMs:1700,
  events:[{valid:true,offset_ms:0,videoTimeMs:2000,audioTimeMs:2000}]};
 const snapshot=receiveReading(null,report,null,10000,100);
 const age=captureReportAge(report,100,120);
 assert.equal(age,1720);
 const view=readingPresentation(snapshot,report,age,120,true);
 assert.equal(view.state,'held');
 assert.equal(savedReading(snapshot,view,10020).savedDisplayState,'held');
 assert.equal(captureReportAge({},100,120),20);
 assert.equal(captureReportAge({},-Infinity,120),Infinity);
});
