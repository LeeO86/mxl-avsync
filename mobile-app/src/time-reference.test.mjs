import test from 'node:test';
import assert from 'node:assert/strict';
import { applyTimeReference, bindReference, clockRadiusText, clockSettings, measurementClockLabel, generatorClockOptions, referenceAt, referenceSummary, synchronizeClock } from './time-reference.mjs';
import { receiveReading, savedReading } from './reading.mjs';
const epoch = 1793450000000;
const sample = {source:'ntp',server:'lab.example',referenceUnixMs:epoch,referenceMonotonicMs:1000,deviceUnixMs:epoch-100,uncertaintyMs:2,roundTripMs:2,clockId:'boot-a',timebase:'ios-host'};
const now = {monotonicMs:1100,clockId:'boot-a',timebase:'ios-host'};
const reference = bindReference(sample,now,10);
const report = {kind:'reading',offset_ms:35,eventCount:4,timeMs:1100,deviceUtcOffsetMs:epoch-1100,
 clockMapping:{...now,deviceUnixMs:epoch},latency:{state:'estimated',video_ms:20,audio_ms:55,source:'live-qr-tai/device-utc'},
 events:[{valid:true,videoTimeMs:1080,audioTimeMs:1115,offset_ms:35}],quality:'good'};
test('native monotonic reference corrects the clock without changing AV sync', () => {
 const result = applyTimeReference(report,reference);
 assert.equal(result.offset_ms,35);
 assert.equal(result.latency.video_ms,120);
 assert.equal(result.latency.audio_ms,155);
 assert.equal(result.latency.raw_video_ms,20);
 assert.equal(result.clockReference.correctionMs,100);
 assert.equal(result.latency.clock.captureUncertaintyMs,null);
 assert.equal(report.latency.video_ms,20);
 assert.deepEqual(applyTimeReference(result,reference),result);
});
test('emit offsets are per essence; only qualified clock references yield estimated absolute latency', () => {
 const value = applyTimeReference(report,reference,clockSettings({sourceEmitVideoMs:-20,sourceEmitAudioMs:-25})).latency;
 assert.equal(value.video_latency_abs_ms,140);assert.equal(value.audio_latency_abs_ms,180);
 assert.equal(value.video_arrival_vs_timestamp_ms,120);
 assert.equal(value.video_source_emit_offset_ms,-20);
 const noClock = applyTimeReference(report,null,clockSettings({sourceEmitVideoMs:-20})).latency;
 assert.equal(noClock.video_latency_abs_ms,undefined);assert.equal(noClock.video_ms,20);
});
test('negative values outside uncertainty flag a mismatch, raw diagnostics survive; zero is not lost', () => {
 const settings=clockSettings({sourceEmitVideoMs:121,sourceEmitAudioMs:200});
 const result=applyTimeReference(report,reference,settings).latency;
 assert.equal(result.video_ms,0);assert.equal(result.video_latency_abs_ms,0);
 assert.equal(result.audio_ms,null);assert.equal(result.audio_latency_abs_ms,undefined);
 assert.equal(result.audio_chain_latency_ms,-45);
 assert.ok(result.flags.includes('audio_clock_or_emit_offset_mismatch'));
});
test('stale, future, incompatible and high-uncertainty references never correct a reading', () => {
 for (const ref of [{...reference,referenceMonotonicMs:-400000},{...reference,referenceMonotonicMs:2000},{...reference,clockId:'other'}, {...reference,uncertaintyMs:21}]) {
  const result=applyTimeReference(report,ref);
  assert.equal(result.latency.video_ms,20);assert.equal(result.clockReference.correctionMs,null);
 }
 assert.equal(referenceAt(reference,now).state,'estimated');
 assert.ok(referenceAt(reference,{...now,monotonicMs:100000}).uncertaintyMs > sample.uncertaintyMs);
 assert.equal(referenceAt(reference,{...now,timebase:'android-elapsed-realtime'}).state,'unavailable');
});
test('a device wall clock step does not move the monotonic UTC reference', () => {
 const stepped={...report,clockMapping:{...report.clockMapping,deviceUnixMs:epoch+2000},latency:{...report.latency,video_ms:2020,audio_ms:2055}};
 assert.equal(applyTimeReference(stepped,reference).latency.video_ms,120);
});
test('saved readings retain their own time correction and clock uncertainty', () => {
 const corrected=applyTimeReference(report,reference);
 const snapshot=receiveReading(null,corrected,null,epoch,10);
 const saved=savedReading(snapshot,{state:'held',ageMs:4000},epoch+4000);
 assert.equal(saved.latency.video_ms,120);assert.equal(saved.clockReference.reference.clockId,'boot-a');
 assert.equal(saved.latency.clock.ageMs,100);assert.equal(saved.raw_offset_ms,35);
 const later=applyTimeReference(report,{...reference,referenceUnixMs:epoch+50});
 assert.equal(later.latency.video_ms,170);assert.equal(saved.latency.video_ms,120);
});
test('clock metadata is retained for audio-only readings and absence of latency is harmless', () => {
 const r={...report,latency:{state:'estimated',audio_ms:55}};
 const result=applyTimeReference(r,reference);
 assert.equal(result.latency.audio_ms,155);assert.equal(result.latency.video_ms,undefined);
 assert.deepEqual(applyTimeReference({kind:'status'},reference),{kind:'status'});
});
test('reference schema and config reject malformed inputs, not silently inventing time', () => {
 for(const value of [{...sample,uncertaintyMs:-1},{...sample,referenceUnixMs:NaN},{...sample,source:'device-utc'},{...sample,clockId:'other'}]) assert.throws(()=>bindReference(value,now));
 for(const value of [{ntpServer:'https://lab'},{clockMaxUncertaintyMs:0},{sourceEmitAudioMs:'NaN'},{sourceEmitVideoMs:1001}]) assert.throws(()=>clockSettings(value));
 assert.equal(clockSettings({sourceEmitVideoMs:'0'}).videoEmitMs,0);
 assert.equal(clockSettings().videoEmitMs,null);
});
test('native acquisition and generator use the same reference, never a JS receipt timestamp', async () => {
 const ref=await synchronizeClock({synchronizeClock:async host=>{assert.equal(host,'lab.example');return sample;},clockNow:()=>now},{ntpServer:'lab.example'},()=>10);
 assert.equal(ref.referenceMonotonicMs,1000);
 assert.equal(generatorClockOptions(ref,now).clockUnixMs,String(epoch));
 assert.deepEqual(generatorClockOptions(ref,{...now,monotonicMs:400000}),{});
 await assert.rejects(synchronizeClock({},{}),/native/);
});
test('GNSS uses its own timebase and summaries expose age, uncertainty and expiry', () => {
 const gnss={...reference,source:'gnss',timebase:'android-elapsed-realtime'};
 assert.equal(referenceAt(gnss,{...now,timebase:'android-elapsed-realtime'}).state,'estimated');
 assert.match(referenceSummary(gnss,100),/GNSS.*estimated clock.*ms/);
 assert.match(referenceSummary(gnss,400000),/expired/);
 assert.match(referenceSummary({...gnss,uncertaintyMs:100},100),/above limit/);
});

test('source-specific emit offsets are not reused for another source', () => {
 const result=applyTimeReference({...report,audioTimecode:{valid:true,crcOk:true,sourceId:2}},reference,clockSettings({sourceEmitVideoMs:-20,emitSourceId:'source:1'}));
 assert.equal(result.latency.video_latency_abs_ms,undefined);
 assert.equal(result.latency.video_ms,120);
 assert.ok(result.latency.flags.includes('source_emit_offsets_belong_to_another_source'));
});

test('small clock uncertainty is not rounded into a claim of zero error', () => {
 assert.equal(clockRadiusText(.001),'<0.1 ms');
 assert.equal(clockRadiusText(2.5),'±2.5 ms');
 assert.equal(clockRadiusText(NaN),'unknown');
});

test('measurement clock label uses frozen acquisition evidence, not a later current reference',()=>{
 const saved=applyTimeReference(report,null).clockReference;
 const current=applyTimeReference(report,reference).clockReference;
 assert.match(measurementClockLabel(saved),/Device UTC.*uncorrected/);
 assert.match(measurementClockLabel(current),/NTP lab.example.*acquisition/);
 assert.match(measurementClockLabel(saved),/Device UTC/);
});
