import test from 'node:test';
import assert from 'node:assert/strict';
import { receiveReading, readingPresentation, savedReading } from './reading.mjs';
const pair = (seq, time, offset = -26) => ({ seq, valid: true, offset_ms: offset, videoTimeMs: time, audioTimeMs: time + offset });
const report = { kind:'reading', timeMs:2400, offset_ms:-26, eventCount:3, spread_ms:6.2,
 audioTimecode:{valid:true,crcOk:true,sourceId:123}, events:[pair(1,1000),pair(2,1500),pair(3,2000)] };
test('brief loss, inconsistent pairs and a capture stall retain a saveable, explicitly aged snapshot', () => {
 const s = receiveReading(null, report, {offset_ms:-1}, 100000, 5000);
 assert.equal(s.reading.offset_ms,-25);
 assert.equal(receiveReading(s,{kind:'status',stage:'inconsistent_pairs'},null,101000,6000),s);
 const held = readingPresentation(s,{kind:'status'},0,7000);
 assert.equal(held.state,'held'); assert.equal(held.ageMs,2400);
 const saved = savedReading(s,held,104000);
 assert.equal(saved.raw_offset_ms,-26); assert.equal(saved.at,100000); assert.equal(saved.savedAt,104000);
 assert.equal(saved.measurementAgeMsAtSave,2400); assert.equal(saved.savedDisplayState,'held');
 assert.equal(readingPresentation(s,report,2200,7200).state,'held');
 assert.equal(readingPresentation(s,report,0,7000,false).state,'paused');
 assert.equal(savedReading(null,{},0),null);
});
test('repeated reporting of the same pair cannot refresh measurement age or overwrite its evidence', () => {
 const s=receiveReading(null,report,null,100000,5000);
 assert.equal(receiveReading(s,{...report,timeMs:3000},null,101000,6000),s);
 assert.equal(readingPresentation(s,report,0,7000).state,'held');
 const next={...report,timeMs:3400,events:[...report.events,pair(4,3000)]};
 const newer=receiveReading(s,next,null,101000,6000);
 assert.notEqual(newer,s);assert.equal(newer.eventTimeMs,3000);
 assert.equal(readingPresentation(newer,next,0,6000).state,'live');
});
test('source change clears the old reading; ordinary status and invalid reports cannot fabricate one', () => {
 const s=receiveReading(null,report,null,100000,5000);
 const changed={kind:'status',audioTimecode:{valid:true,crcOk:true,sourceId:456}};
 assert.equal(receiveReading(s,changed,null,101000,6000),null);
 assert.equal(receiveReading(null,{kind:'status'},null,0,0),null);
 assert.equal(receiveReading(s,{...report,offset_ms:NaN},null,0,0),s);
 assert.equal(readingPresentation(null,{},0,0).state,'empty');
});

test('QR identity changes also clear a retained reading when the audio lease is absent', () => {
 const first={...report,signalIdentity:'source-a'};
 const s=receiveReading(null,first,null,100000,5000);
 assert.equal(receiveReading(s,{kind:'status',signalIdentity:'source-b'},null,101000,6000),null);
 const invalidAudio={kind:'status',audioTimecode:{valid:true,crcOk:false,sourceId:456}};
 assert.equal(receiveReading(s,invalidAudio,null,101000,6000),s);
 const last=readingPresentation(s,{kind:'status'},0,35000);
 assert.equal(last.state,'held');assert.equal(last.ageMs,30400);
 assert.equal(savedReading(s,last,130000).offset_ms,-26);
});
