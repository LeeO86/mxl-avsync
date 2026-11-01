import test from 'node:test';
import assert from 'node:assert/strict';
import {CalibrationPlayback,reviewTimeline,waveformPosition,repeatInterval,waveformBins} from './calibration-playback.mjs';
const take={frames:[{timeMs:1000},{timeMs:1010},{timeMs:1055},{timeMs:1100}],spikeTimeMs:1040,uri:'fixture.wav',audioStartMs:995,waveformStartMs:995,waveformDurationMs:120,durationMs:1115};
const turn=()=>new Promise(resolve=>setImmediate(resolve));
function fake(){let callback,unloads=0,commands=[];return {create:async cb=>{callback=cb;return {unloadAsync:async()=>{unloads++;},setStatusAsync:async status=>{commands.push(status);return {...status,isLoaded:true,isPlaying:status.shouldPlay};}}},emit:s=>callback(s),get commands(){return commands;},get unloads(){return unloads;}};}
test('review and seeks use capture timestamps with a nonzero WAV origin and uneven frames',()=>{
 assert.deepEqual(reviewTimeline(take),{start:1000,end:1100,audioStart:995});
 assert.equal(waveformPosition(50,100,reviewTimeline(take)),1050);
 assert.equal(waveformPosition(-20,100,reviewTimeline(take)),1000);
 assert.equal(waveformPosition(120,100,reviewTimeline(take)),1100);
 assert.equal(repeatInterval(400),90);assert.equal(repeatInterval(1300),40);
});
test('shared audio/video playback, pause/seek, per-frame selection, end and replay',async()=>{
 const sound=fake();let now=0;const driver=new CalibrationPlayback(take,sound.create,()=>{},()=>now);
 driver.toggle();await turn();assert.equal(driver.playing,true);assert.equal(sound.commands[0].positionMillis,5);
 now=20;driver.tick();assert.equal(driver.position,1020);
 driver.step(-1);await turn();assert.equal(driver.playing,false);assert.equal(driver.position,1000);
 driver.seek(1100);await turn();driver.toggle();await turn();assert.equal(driver.position,1000);assert.equal(driver.playing,true);
 now+=110;driver.tick();await turn();assert.equal(driver.position,1100);assert.equal(driver.playing,false);
 driver.dispose();await turn();assert.equal(sound.unloads,1);
});
test('fast held-arrow seeks coalesce while loading, and disposal cancels late loads',async()=>{
 let resolve;const loaded=new Promise(r=>resolve=r);let commands=[],unloads=0;
 const driver=new CalibrationPlayback(take,()=>loaded,()=>{});
 driver.seek(1000);driver.seek(1010);driver.seek(1055);resolve({setStatusAsync:async s=>{commands.push(s);return {...s,isLoaded:true,isPlaying:false};},unloadAsync:async()=>unloads++});await turn();
 assert.equal(commands.length,1);assert.equal(commands[0].positionMillis,60);driver.dispose();
 let finish;const pending=new Promise(r=>finish=r);const closed=new CalibrationPlayback(take,()=>pending,()=>{});closed.toggle();closed.dispose();finish({unloadAsync:async()=>unloads++});await turn();assert.equal(unloads,2);
});
test('buffering stops extrapolation and explicit errors keep manual frames usable',async()=>{
 const sound=fake();let now=0;const driver=new CalibrationPlayback(take,sound.create,()=>{},()=>now);driver.toggle();await turn();sound.emit({isLoaded:true,isPlaying:false,isBuffering:true,positionMillis:60});now=500;driver.tick();assert.equal(driver.position,1055);driver.dispose();
 const failed=new CalibrationPlayback(take,async()=>{throw Error('fixture failure');},()=>{});failed.toggle();await turn();assert.equal(failed.playing,false);assert.match(failed.error,/fixture failure/);failed.step(1);assert.equal(failed.snapshot().selected,1);failed.dispose();
});

test('waveform geometry preserves original bin positions when the review window is cropped',()=>{
 const bins=waveformBins({waveform:[.1,.8,.2],waveformStartMs:900,waveformDurationMs:300,waveformBinDurationMs:100},{start:1050,end:1150});
 assert.deepEqual(bins,[{value:.8,left:0,width:50},{value:.2,left:50,width:50}]);
});

test('initial playback includes the contact even if the preset selected frame is just after it',async()=>{
 const sound=fake();const driver=new CalibrationPlayback(take,sound.create,()=>{});
 assert.ok(driver.position>take.spikeTimeMs);driver.toggle();await turn();assert.ok(driver.position<take.spikeTimeMs);driver.seek(1055);await turn();driver.toggle();await turn();assert.equal(driver.position,1055);await driver.dispose();
});
