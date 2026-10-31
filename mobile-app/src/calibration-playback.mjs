import {nearestFrame} from './workflow.mjs';
export const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
export function reviewTimeline(take) {
 const audioStart=take.audioStartMs ?? take.waveformStartMs ?? 0;
 const audioEnd=audioStart+(take.waveformDurationMs || take.durationMs);
 return {start:Math.max(take.frames[0].timeMs,audioStart),end:Math.min(take.frames.at(-1).timeMs,audioEnd),audioStart};
}
export function waveformPosition(x,width,timeline) {return timeline.start+clamp(x/Math.max(1,width),0,1)*(timeline.end-timeline.start);}
export function waveformBins(take,timeline) {
 const values=take.waveform||[],start=take.waveformStartMs||0,duration=take.waveformDurationMs||take.durationMs;
 const interval=take.waveformBinDurationMs||duration/Math.max(1,values.length),span=Math.max(1,timeline.end-timeline.start);
 return values.flatMap((value,i)=>{const left=Math.max(timeline.start,start+i*interval),right=Math.min(timeline.end,start+duration,start+(i+1)*interval);return right>left?[{value,left:(left-timeline.start)/span*100,width:(right-left)/span*100}]:[];});
}
export function repeatInterval(heldMs) {return heldMs>=1200 ? 40 : 90;}
// The media clock drives preview only. Calibration always saves an original frame timestamp.
export class CalibrationPlayback {
 constructor(take,createSound,onChange,now=()=>performance.now()) {
  Object.assign(this,{take,createSound,onChange,now});this.timeline=reviewTimeline(take);
  this.position=clamp(take.frames[nearestFrame(take.frames,take.spikeTimeMs)].timeMs,this.timeline.start,this.timeline.end);
  this.playing=false;this.clockRunning=false;this.closed=false;this.revision=0;this.pending=null;this.busy=false;
 }
 snapshot(){return {position:this.position,selected:nearestFrame(this.take.frames,this.position),playing:this.playing,error:this.error};}
 notify(){if(!this.closed)this.onChange(this.snapshot());}
 seek(position,playing=false){
  if(this.closed)return;
  this.position=clamp(position,this.timeline.start,this.timeline.end);this.playing=playing;this.clockRunning=false;this.error=null;
  this.pending={position:this.position,playing,revision:++this.revision};this.notify();if(!this.busy)this.job=this.pump();
 }
 step(direction){const i=clamp(this.snapshot().selected+direction,0,this.take.frames.length-1);this.seek(this.take.frames[i].timeMs);}
 toggle(){this.seek(this.position>=this.timeline.end-1 && !this.playing ? this.timeline.start : this.position,!this.playing);}
 tick(){
  if(!this.clockRunning||this.busy||!this.playing||this.closed)return;
  this.position=clamp(this.anchor.position+this.now()-this.anchor.at,this.timeline.start,this.timeline.end);
  if(this.position>=this.timeline.end){this.seek(this.timeline.end,false);return;}
  this.notify();
 }
 status(status,fromCommand=false){
  if(this.closed||(this.busy&&!fromCommand)||!status.isLoaded)return;
  if(status.didJustFinish && this.playing){this.seek(this.timeline.end,false);return;}
  this.position=clamp(this.timeline.audioStart+status.positionMillis,this.timeline.start,this.timeline.end);
  this.clockRunning=this.playing && status.isPlaying && !status.isBuffering;
  this.anchor={position:this.position,at:this.now()};this.notify();
 }
 async pump(){
  if(this.busy||this.closed)return;this.busy=true;
  try {
   if(!this.sound){
    const sound=await this.createSound(status=>this.status(status));
    if(this.closed){await sound.unloadAsync();return;}
    this.sound=sound;
   }
   while(this.pending && !this.closed){
    const command=this.pending;this.pending=null;
    const status=await this.sound.setStatusAsync({positionMillis:Math.max(0,command.position-this.timeline.audioStart),shouldPlay:command.playing,progressUpdateIntervalMillis:33,seekMillisToleranceBefore:0,seekMillisToleranceAfter:0});
    if(this.closed)return;
    if(command.revision===this.revision){this.status(status,true);}
   }
  } catch(error){if(!this.closed){this.playing=false;this.clockRunning=false;this.pending=null;this.error=`Playback unavailable: ${error.message}`;this.notify();}}
  finally{this.busy=false;}
 }
 dispose(){
  if(this.disposal)return this.disposal;
  this.closed=true;this.pending=null;this.playing=false;this.clockRunning=false;
  this.disposal=(async()=>{await this.job;await this.sound?.unloadAsync().catch(()=>{});})();
  return this.disposal;
 }
}
