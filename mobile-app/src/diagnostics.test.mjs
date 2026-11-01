import test from 'node:test';
import assert from 'node:assert/strict';
import {diagnosticSnapshot} from './diagnostics.mjs';

test('diagnostic shares distinguish held acquisition from current capture and exclude calibration media',()=>{
 const reading={kind:'reading',offset_ms:12,clockReference:{source:'ntp',correctionMs:100},at:1000};
 const reference={source:'ntp',offsetMs:105};
 const report={kind:'calibration',captureStats:{maxFrameGapMs:42},qrFlash:{trace:[{timeMs:50,level:.8,phase:'white'}]},uri:'file:///private/clap.wav',frames:[{uri:'file:///private/frame.jpg'}],waveform:[.1,.5],picture:{found:true,points:[{x:.1,y:.2}],uri:'private-image'},session:{password:'excluded'}};
 const exported=JSON.parse(JSON.stringify(diagnosticSnapshot({report,ageMs:4000,active:false,reference,reading,readingAgeMs:42000,exportedAt:50000})));
 assert.equal(exported.currentReportAgeMs,4000);assert.equal(exported.lastAcceptedReadingAgeMs,42000);
 assert.equal(exported.lastAcceptedReading.clockReference.correctionMs,100);
 assert.equal(exported.currentClockReference.offsetMs,105);
 assert.equal(exported.currentReport.captureStats.maxFrameGapMs,42);
 assert.deepEqual(exported.currentReport.qrFlash.trace,[{timeMs:50,level:.8,phase:'white'}]);
 assert.equal(exported.currentReport.uri,undefined);assert.equal(exported.currentReport.frames,undefined);
 assert.equal(exported.currentReport.waveform,undefined);assert.equal(exported.currentReport.session,undefined);
 assert.equal(exported.currentReport.picture.uri,undefined);
 assert.deepEqual(reading,{kind:'reading',offset_ms:12,clockReference:{source:'ntp',correctionMs:100},at:1000});
});
