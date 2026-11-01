#import "AVSyncStreamAnalyzer.h"
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>
#include "avsync/avsync.hpp"
#include "AVSyncLatency.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <chrono>
#include <memory>
#if DEBUG || AVSYNC_DIAGNOSTIC_CAPTURE
#include "DiagnosticCapture.inc"
#endif

namespace {
double work_now_ms() { return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
struct Frame { double time; int w,h; std::vector<uint8_t> luma; };
struct StreamValues {
 bool calibration=false,complete=false;
 double origin=-1,lastVideo=-1,lastAudio=-1,lastReport=-1,spike=-1,floor=0.002;
 int frames=0,rate=48000,previousLocated=0,previousValid=0,videoWidth=0,videoHeight=0;
 std::optional<avsync::QrPayload> qr;
 double qrObservedMs=-1e30,wallOffsetMs=0,taiUtcSeconds=37;
 bool haveWallClock=false;
 bool legacyAcquired=false;
#if DEBUG || AVSYNC_DIAGNOSTIC_CAPTURE
 std::shared_ptr<avsync_phone::DiagnosticCapture> diagnostic=avsync_phone::diagnostic_capture();
#endif
 std::deque<Frame> pictures;
 std::vector<float> audio;
 double audioOrigin=-1;
 double lastUnidentifiedVideo=-1e30;
};
struct Stream: StreamValues {
 std::unique_ptr<avsync::PhoneSession> session;
 Stream(){session=std::make_unique<avsync::PhoneSession>(2,avsync::GradeWindows{});}
 Stream(const Stream& other):StreamValues(other),session(std::make_unique<avsync::PhoneSession>(*other.session)){}
};
NSData *wav(const std::vector<float>& samples,int rate) {
 uint32_t length=(uint32_t)samples.size()*2, size=36+length, fmt=16, bytes=rate*2;
 uint16_t pcm=1,channels=1,align=2,bits=16;
 NSMutableData *data=[NSMutableData data];
 auto add=[&](const void*p,size_t n){[data appendBytes:p length:n];};
 add("RIFF",4);add(&size,4);add("WAVEfmt ",8);add(&fmt,4);add(&pcm,2);add(&channels,2);add(&rate,4);add(&bytes,4);add(&align,2);add(&bits,2);add("data",4);add(&length,4);
 for(float x:samples){int16_t value=(int16_t)std::round(std::clamp(x,-1.f,1.f)*32767);add(&value,2);}
 return data;
}
}
@implementation AVSyncStreamAnalyzer { std::unique_ptr<Stream> state; BOOL sparseAcquisition; }
- (instancetype)init { if((self=[super init])) [self reset]; return self; }
- (void)reset { state=std::make_unique<Stream>(); }
- (void)setSparseAcquisition:(BOOL)enabled { sparseAcquisition=enabled; }
- (void)beginCalibration { [self reset];state->calibration=true; }
- (void)setTaiUtcOffset:(double)seconds { if(std::isfinite(seconds)&&seconds>=0&&seconds<=100)state->taiUtcSeconds=seconds; }
- (void)setClockAnchor:(double)captureTimeMs unixTimeMs:(double)unixTimeMs {
 if(!std::isfinite(captureTimeMs)||!std::isfinite(unixTimeMs))return;
 double offset=unixTimeMs-captureTimeMs;
 if(state->haveWallClock&&std::abs(offset-state->wallOffsetMs)>100)state->qr.reset();
 state->wallOffsetMs=offset;state->haveWallClock=true;
#if DEBUG || AVSYNC_DIAGNOSTIC_CAPTURE
 if(state->diagnostic)state->diagnostic->note(@{@"kind":@"clock",@"captureMs":@(captureTimeMs),@"unixMs":@(unixTimeMs),@"taiUtcSeconds":@(state->taiUtcSeconds)});
#endif
}
- (void)observeQR:(NSString *)text timeMs:(double)timeMs {
 auto qr=avsync::parse_qr_payload(text.UTF8String);
 if(qr&&std::isfinite(timeMs)){state->session->observe_signal(*qr);state->qr=qr;state->qrObservedMs=timeMs;
#if DEBUG || AVSYNC_DIAGNOSTIC_CAPTURE
  if(state->diagnostic)state->diagnostic->note(@{@"kind":@"qr",@"timeMs":@(timeMs),@"payload":text});
#endif
 }
}
- (void)observeQRDetection:(NSDictionary *)detection timeMs:(double)timeMs {
 NSString *text=detection[@"payload"];NSArray *points=detection[@"points"];
 auto qr=avsync::parse_qr_payload(text.UTF8String);
 if(!qr||points.count!=4)return;
 [self observeQR:text timeMs:timeMs];
 avsync::QrDetection q;q.payload=*qr;
 for(int i=0;i<4;++i){q.x[i]=[points[i][@"x"] doubleValue];q.y[i]=[points[i][@"y"] doubleValue];}
 state->session->observe_qr(q,timeMs);
}
- (NSDictionary *)pictureObservation {
 auto&s=*state;const auto&pose=s.session->current_picture();
 if(s.calibration||s.lastAudio-s.lastVideo>300||!pose.found||s.videoWidth<=0||s.videoHeight<=0)return @{@"found":@NO};
 NSMutableArray *points=[NSMutableArray array];
 for(int i=0;i<4;++i)[points addObject:@{@"x":@(pose.corner_x[i]/s.videoWidth),@"y":@(pose.corner_y[i]/s.videoHeight)}];
 auto confirmed=s.session->confirmed_picture_seq();
 bool decoded=confirmed&&s.lastVideo-s.session->confirmed_picture_time_ms()<=150;
 NSMutableDictionary *result=[@{@"found":@YES,@"codeDecoded":@(decoded),@"points":points,@"width":@(s.videoWidth),@"height":@(s.videoHeight)}mutableCopy];
 if(decoded)result[@"codeSeq"]=@(*confirmed);
 return result;
}
- (BOOL)qrScanSuggestedAt:(double)timeMs { return !state->calibration && state->session->qr_scan_suggested(timeMs); }
- (AVSyncVideoWork)pushVideo:(CMSampleBufferRef)sample {
 auto&s=*state;
 AVSyncVideoWork work={0,0,0,0};
 double time=CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sample))*1000;
 if(!std::isfinite(time))return work;
 if(s.origin<0)s.origin=time;
 s.lastVideo=time;++s.frames;
 // Before any QR identity, retain only a 10-Hz legacy acquisition attempt.
 // The independent Vision worker acquires AVS2; 240 full-raster searches/s
 // on an unidentified scene cannot contribute a trusted AVS2 timestamp.
 if(sparseAcquisition&&!s.calibration&&!s.qr&&!s.legacyAcquired&&time-s.lastUnidentifiedVideo<100){work.path=4;return work;}
 s.lastUnidentifiedVideo=time;
 CVPixelBufferRef pixel=CMSampleBufferGetImageBuffer(sample);if(!pixel)return work;
 const double lockStart=work_now_ms();
 const auto locked=CVPixelBufferLockBaseAddress(pixel,kCVPixelBufferLock_ReadOnly);
 work.lockMs=work_now_ms()-lockStart;
 if(locked!=kCVReturnSuccess)return work;
 const double processStart=work_now_ms();
 int w=(int)CVPixelBufferGetWidthOfPlane(pixel,0),h=(int)CVPixelBufferGetHeightOfPlane(pixel,0),stride=(int)CVPixelBufferGetBytesPerRowOfPlane(pixel,0);
 const uint8_t*base=(const uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pixel,0);
 int limit=s.calibration?640:1280,step=std::max(1,(std::max(w,h)+limit-1)/limit),sw=w/step,sh=h/step;
 s.videoWidth=sw;s.videoHeight=sh;
 if(s.calibration){
  work.path=3;
  Frame f{time,sw,sh,std::vector<uint8_t>(sw*sh)};
  for(int y=0;y<sh;y++)for(int x=0;x<sw;x++)f.luma[y*sw+x]=base[y*step*stride+x*step];
  s.pictures.push_back(std::move(f));
  double oldest=s.spike>=0?s.spike-750:time-750;
  while(!s.pictures.empty()&&(s.pictures.front().time<oldest||s.pictures.size()>800))s.pictures.pop_front();
 }else if(s.session->qr_flash_active()){
  work.path=2;
  // Sparse photometry directly in the camera plane; no conversion/rectification.
  s.videoWidth=w;s.videoHeight=h;
  s.session->push_video_u8(base,w,h,stride,time);
#if DEBUG || AVSYNC_DIAGNOSTIC_CAPTURE
  if(s.diagnostic&&s.diagnostic->accepting()){
   std::vector<uint16_t> luma(sw*sh);
   for(int y=0;y<sh;y++)for(int x=0;x<sw;x++)luma[y*sw+x]=base[y*step*stride+x*step]*4;
   NSMutableDictionary *trace=[@{@"locatedFrames":@(s.session->located_frames()),@"validFrames":@(s.session->valid_frames()),@"picture":[self pictureObservation]} mutableCopy];
   s.diagnostic->video(std::move(luma),sw,sh,time,trace);
  }
#endif
 }else{
  work.path=1;
  std::vector<uint16_t> luma(sw*sh);
  for(int y=0;y<sh;y++)for(int x=0;x<sw;x++)luma[y*sw+x]=base[y*step*stride+x*step]*4;
  s.session->push_video(luma.data(),sw,sh,sw,time,0);
  s.legacyAcquired=s.session->current_picture().found;
#if DEBUG || AVSYNC_DIAGNOSTIC_CAPTURE
  if(s.diagnostic&&s.diagnostic->accepting()){
   NSMutableDictionary *trace=[@{@"locatedFrames":@(s.session->located_frames()),@"validFrames":@(s.session->valid_frames()),@"picture":[self pictureObservation]} mutableCopy];
   auto event=s.session->latest_video_event();if(event){trace[@"eventSeq"]=@(event->seq);trace[@"eventTimeMs"]=@(event->time_ms);}
   s.diagnostic->video(std::move(luma),sw,sh,time,trace);
  }
#endif
 }
 work.processMs=work_now_ms()-processStart;
 const double unlockStart=work_now_ms();
 CVPixelBufferUnlockBaseAddress(pixel,kCVPixelBufferLock_ReadOnly);
 work.unlockMs=work_now_ms()-unlockStart;
 return work;
}
- (void)pushAudio:(CMSampleBufferRef)sample {
 auto&s=*state;
 double time=CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sample))*1000;
 if(!std::isfinite(time))return;
 const AudioStreamBasicDescription *format=CMAudioFormatDescriptionGetStreamBasicDescription(CMSampleBufferGetFormatDescription(sample));
 if(!format||format->mFormatID!=kAudioFormatLinearPCM)return;
 s.rate=(int)format->mSampleRate;
 CMBlockBufferRef block=CMSampleBufferGetDataBuffer(sample);if(!block)return;
 size_t size=CMBlockBufferGetDataLength(block);std::vector<uint8_t> bytes(size);
 if(CMBlockBufferCopyDataBytes(block,0,size,bytes.data())!=kCMBlockBufferNoErr)return;
 size_t count=CMSampleBufferGetNumSamples(sample);
 int channels=(int)format->mChannelsPerFrame,bits=(int)format->mBitsPerChannel;
 bool floating=format->mFormatFlags&kAudioFormatFlagIsFloat;
 int stride=(format->mFormatFlags&kAudioFormatFlagIsNonInterleaved)?1:channels;
 if(channels<=0||(bits!=16&&bits!=32)||size<count*stride*(bits/8))return;
 std::vector<float> samples(count);
 for(size_t i=0;i<count;i++){
  size_t index=i*stride;
  if(floating&&bits==32)samples[i]=((const float*)bytes.data())[index];
  else if(bits==16)samples[i]=((const int16_t*)bytes.data())[index]/32768.f;
  else samples[i]=((const int32_t*)bytes.data())[index]/2147483648.f;
 }
 if(s.origin<0)s.origin=time;
 s.lastAudio=time+samples.size()*1000.0/s.rate;
 if(s.calibration){
  if(s.audioOrigin<0)s.audioOrigin=time;
  size_t at=(size_t)std::max(0.0,std::round((time-s.audioOrigin)*s.rate/1000));
  if(at>s.audio.size()&&at<18*s.rate)s.audio.resize(at,0);
  s.audio.insert(s.audio.end(),samples.begin(),samples.end());
  float peak=0;size_t peakIndex=0;double power=0;
  for(size_t i=0;i<samples.size();i++){power+=samples[i]*samples[i];if(std::abs(samples[i])>peak){peak=std::abs(samples[i]);peakIndex=i;}}
  double rms=std::sqrt(power/std::max((size_t)1,samples.size()));
  if(s.spike<0 && time-s.origin>750 && peak>std::max(0.08,s.floor*8) && peak>rms*1.8)
   s.spike=time+peakIndex*1000.0/s.rate;
  if(s.spike<0)s.floor=0.97*s.floor+0.03*rms;
 }else{
  s.session->push_audio(samples.data(),samples.size(),time,s.rate,true);
#if DEBUG || AVSYNC_DIAGNOSTIC_CAPTURE
  if(s.diagnostic)s.diagnostic->audio(std::move(samples),s.rate,time);
#endif
 }
}
- (BOOL)acceptLiveAnalysis:(AVSyncStreamAnalyzer *)snapshot {
 const auto&a=state->qr;const auto&b=snapshot->state->qr;
 if(bool(a)!=bool(b)||(a&&b&&(a->source_id!=b->source_id||a->test_id!=b->test_id)))return NO;
 state->session->merge_audio_analysis(*snapshot->state->session);
 return YES;
}
- (NSDictionary *)pollCalibration { return state->calibration ? [self poll] : nil; }
- (AVSyncStreamAnalyzer *)liveSnapshotForPolling {
 auto&s=*state;
 const double now=std::max(s.lastVideo,s.lastAudio);
 if(s.calibration||s.origin<0||now-s.origin<1500||now-s.lastReport<1000)return nil;
 AVSyncStreamAnalyzer *snapshot=[AVSyncStreamAnalyzer new];
 snapshot->state=std::make_unique<Stream>(s); // All mutable DSP buffers are independent.
 s.lastReport=now;
 s.previousLocated=s.session->located_frames();s.previousValid=s.session->valid_frames();
 s.session->trim_before(now-12000); // Maintain the same bounded history as synchronous polling.
 return snapshot;
}
- (NSDictionary *)poll {
 auto&s=*state;
 if(s.origin<0)return nil;
 double now=s.calibration?std::min(s.lastVideo,s.lastAudio):std::max(s.lastVideo,s.lastAudio),duration=now-s.origin;
 if(s.calibration){
  if(s.complete)return nil;
  if(s.spike<0){if(duration>12000){s.complete=true;return @{@"kind":@"error",@"message":@"No sharp sound detected. Try one visible clap after a quiet pause."};}return nil;}
  if(now<s.spike+700)return nil;
  s.complete=true;
  int before=0,after=0;
  for(const auto&f:s.pictures){if(f.time<s.spike)++before;else++after;}
  if(before<2||after<2)return @{@"kind":@"error",@"message":@"Not enough camera frames before AND after the sound. Repeat calibration."};
  NSString *dir=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
  [[NSFileManager defaultManager]createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
  NSMutableArray *frames=[NSMutableArray array];
  for(const auto&f:s.pictures){
   NSData *data=[NSData dataWithBytes:f.luma.data() length:f.luma.size()];
   CGDataProviderRef provider=CGDataProviderCreateWithCFData((__bridge CFDataRef)data);
   CGColorSpaceRef space=CGColorSpaceCreateDeviceGray();
   CGImageRef image=CGImageCreate(f.w,f.h,8,8,f.w,space,kCGImageAlphaNone,provider,nil,false,kCGRenderingIntentDefault);
   NSString *path=[dir stringByAppendingPathComponent:[NSString stringWithFormat:@"%lu.jpg",(unsigned long)frames.count]];
   if(image&&[UIImageJPEGRepresentation([UIImage imageWithCGImage:image],0.85)writeToFile:path atomically:YES])
    [frames addObject:@{@"timeMs":@(f.time-s.origin),@"uri":[NSURL fileURLWithPath:path].absoluteString}];
   if(image)CGImageRelease(image);CGColorSpaceRelease(space);CGDataProviderRelease(provider);
  }
  if(frames.count!=s.pictures.size())return @{@"kind":@"error",@"message":@"Could not save calibration frames. Check available storage and repeat."};
  const double clipStart=std::max(s.audioOrigin,s.pictures.front().time);
  const double clipEnd=std::min(s.lastAudio,s.pictures.back().time+1000.0/std::max(1.0,s.frames*1000.0/std::max(1.0,s.lastVideo-s.origin)));
  const size_t first=std::min(s.audio.size(),size_t(std::max(0.0,std::round((clipStart-s.audioOrigin)*s.rate/1000))));
  const size_t last=std::clamp(size_t(std::max(0.0,std::round((clipEnd-s.audioOrigin)*s.rate/1000))),first,s.audio.size());
  std::vector<float> reviewAudio(s.audio.begin()+first,s.audio.begin()+last);
  const double audioStart=s.audioOrigin+first*1000.0/s.rate-s.origin;
  NSMutableArray *wave=[NSMutableArray array];size_t bucket=std::max((size_t)1,reviewAudio.size()/256);
  for(size_t i=0;i<reviewAudio.size();i+=bucket){float peak=0;for(size_t j=i;j<std::min(reviewAudio.size(),i+bucket);j++)peak=std::max(peak,std::abs(reviewAudio[j]));[wave addObject:@(peak)];}
  NSString* path=[dir stringByAppendingPathComponent:@"sound.wav"];
  if(![wav(reviewAudio,s.rate)writeToFile:path atomically:YES])return @{@"kind":@"error",@"message":@"Could not save calibration audio. Check available storage and repeat."};
  double fps=s.frames*1000.0/std::max(1.0,s.lastVideo-s.origin);
  return @{@"kind":@"calibration",@"frames":frames,@"spikeTimeMs":@(s.spike-s.origin),@"fps":@(fps),@"framesBefore":@(before),@"framesAfter":@(after),@"waveform":wave,@"durationMs":@(duration),@"audioStartMs":@(audioStart),@"waveformStartMs":@(audioStart),@"waveformBinDurationMs":@(bucket*1000.0/s.rate),@"waveformDurationMs":@(reviewAudio.size()*1000.0/s.rate),@"uri":[NSURL fileURLWithPath:path].absoluteString};
 }
 if(duration<1500||now-s.lastReport<1000)return nil;
 s.lastReport=now;
 auto report=s.session->finish("PHONE");std::vector<double>offsets;
 const double recentWindow=s.qr?std::max(2000.0,3500.0/std::max(1,s.qr->rate)):2000.0;
 NSMutableArray *events=[NSMutableArray array];
 for(const auto&e:report.events){NSMutableDictionary*item=[@{@"seq":@(e.seq),@"videoTimeMs":@(e.video_time_ms),@"audioTimeMs":@(e.audio_time_ms),@"reason":[NSString stringWithUTF8String:e.reason.c_str()],@"valid":@(e.grade!="invalid")}mutableCopy];if(e.av_offset_ms)item[@"offset_ms"]=@(*e.av_offset_ms);[events addObject:item];}
 for(const auto&e:report.events)if(e.av_offset_ms&&e.grade!="invalid"&&std::isfinite(*e.av_offset_ms)&&std::max(e.video_time_ms,e.audio_time_ms)>=now-recentWindow)offsets.push_back(*e.av_offset_ms);
 double fps=s.frames*1000.0/std::max(1.0,s.lastVideo-s.origin);
 NSMutableDictionary* result=[@{@"kind":@"status",@"fps":@(fps),@"locatedFrames":@(s.session->located_frames()),@"validFrames":@(s.session->valid_frames()),@"eventCount":@(offsets.size()),@"events":events}mutableCopy];
 const int recentLocated=s.session->located_frames()-s.previousLocated,recentValid=s.session->valid_frames()-s.previousValid;
 s.previousLocated=s.session->located_frames();s.previousValid=s.session->valid_frames();
 NSMutableArray *chirps=[NSMutableArray array];double bestCorrelation=0;
 for(const auto&hit:report.audio_hits){bestCorrelation=std::max(bestCorrelation,hit.correlation);[chirps addObject:@{@"id":@(hit.chirp_id),@"timeMs":@(hit.time_ms),@"correlation":@(hit.correlation),@"quality":[NSString stringWithUTF8String:hit.quality.c_str()]}];}
 result[@"timeMs"]=@(now);result[@"picture"]=[self pictureObservation];
 if(s.session->qr_flash_active()){
  const auto&d=s.session->qr_flash_diagnostics();
  NSMutableArray *trace=[NSMutableArray array];
  for(const auto&sample:d.samples)[trace addObject:@{@"timeMs":@(sample.time_ms),@"level":@(sample.level),@"range":@(sample.range),@"phase":sample.phase==2?@"white":sample.phase==1?@"mixed":@"black"}];
  NSMutableArray *details=[NSMutableArray array];
  NSArray *reasons=@[@"waiting_for_second_white",@"waiting_for_qr",@"qr_time_window",@"qr_seen_before_edge",@"qr_pixels_unconfirmed",@"duplicate_sequence",@"confirmed"];
  for(const auto&e:d.edge_details){
   NSMutableDictionary *row=[@{@"edgeId":@(e.id),@"whiteTimeMs":@(e.white_ms),@"reason":reasons[std::clamp(e.reason,0,6)],@"matchAttempts":@(e.attempts),@"knownModules":@(e.max_known_modules)} mutableCopy];
   NSArray *keys=@[@"threshold10Ms",@"threshold30Ms",@"threshold50Ms",@"threshold70Ms"];
   for(int i=0;i<4;++i)if(e.crossings_ms[i]>=0)row[keys[i]]=@(e.crossings_ms[i]);
   if(e.first_qr_ms>=0){row[@"candidateSeq"]=@(e.sequence);row[@"firstQrTimeMs"]=@(e.first_qr_ms);}
   [details addObject:row];
  }
  result[@"qrFlash"]=@{@"scope":@"since source/reset",@"timestampPolicy":@"white70-v1",@"frames":@(d.frames),@"probeFrames":@(d.probe_frames),@"symbolFrames":@(d.symbol_frames),@"moduleReads":@(d.module_reads),@"matchCalls":@(d.match_calls),@"symbolComparisons":@(d.symbol_comparisons),@"mixedFrames":@(d.mixed_frames),@"finderRejections":@(d.finder_rejections),@"geometryRejections":@(d.geometry_rejections),@"edges":@(d.edges),@"emitted":@(d.emitted),@"trace":trace,@"edgeDetails":details};
 }
 if(s.haveWallClock)result[@"deviceUtcOffsetMs"]=@(s.wallOffsetMs);
 if(s.qr)result[@"signalIdentity"]=[NSString stringWithUTF8String:(s.qr->test_id+":"+std::to_string(s.qr->source_id)).c_str()];
 NSMutableDictionary *latency=[@{@"state":@"waiting_for_live_qr",@"estimated":@YES,@"taiUtcSeconds":@(s.taiUtcSeconds),@"clockAccuracy":@"unknown",@"videoState":@"waiting_for_live_qr",@"audioState":@"waiting_for_audio_timecode"}mutableCopy];
 if(s.qr){
  const auto video=s.session->latest_video_event();
  auto source=avsync_phone::source_time(*s.qr,s.qrObservedMs,now,video,s.taiUtcSeconds);
  latency[@"state"]=latency[@"videoState"]=[NSString stringWithUTF8String:source.state];
  latency[@"qrAgeMs"]=@(now-s.qrObservedMs);latency[@"qrSeq"]=@(s.qr->k);
  if(video)latency[@"videoSeq"]=@(video->seq);
  if(source.video_utc_ms&&video&&s.haveWallClock){
   latency[@"state"]=latency[@"videoState"]=@"estimated";latency[@"videoSource"]=@"live-qr-tai/device-utc";latency[@"video_ms"]=@(video->time_ms+s.wallOffsetMs-*source.video_utc_ms);
   for(auto it=report.events.rbegin();it!=report.events.rend();++it)if(!s.qr->timecode&&it->seq==video->seq&&it->av_offset_ms&&it->grade!="invalid"&&source.audio_utc_ms){latency[@"audio_ms"]=@(it->audio_time_ms+s.wallOffsetMs-*source.audio_utc_ms);latency[@"audioState"]=@"estimated";break;}
   latency[@"source"]=@"live-qr-tai/device-utc";latency[@"eventSeq"]=@(video->seq);latency[@"referenceAgeMs"]=@(now-s.qrObservedMs);
  }else if(source.video_utc_ms)latency[@"state"]=latency[@"videoState"]=@"waiting_for_device_clock";
 }
 if(!report.audio_timecodes.empty()){
  const auto&packet=report.audio_timecodes.back();
  const double lease=1000+avsync::timecode_duration_ms(packet.bytes.size(),packet.baud)+200;
  if(now-packet.time_ms<=lease&&packet.time_ms<=now+100){
   result[@"audioTimecode"]=@{@"valid":@YES,@"crcOk":@YES,@"receivedTimeMs":@(packet.time_ms),@"validUntilMs":@(packet.time_ms+lease),@"sourceId":@(packet.source_id),@"baud":@(packet.baud),@"live":@(packet.live),@"sendTaiNs":[NSString stringWithFormat:@"%lld",(long long)packet.audio_send_ns],@"videoSendTaiNs":[NSString stringWithFormat:@"%lld",(long long)packet.video_send_ns]};
   if(packet.live&&s.haveWallClock){
    latency[@"state"]=latency[@"audioState"]=@"estimated";
    latency[@"audio_ms"]=@(packet.time_ms+s.wallOffsetMs+s.taiUtcSeconds*1000-double(packet.audio_send_ns)/1e6);
    latency[@"audioSource"]=@"audio-timecode-tai/device-utc";
    if(!latency[@"video_ms"]){
     for(auto it=report.events.rbegin();it!=report.events.rend();++it){
      auto paired=avsync_phone::paired_video_latency(packet,*it,now,s.wallOffsetMs,s.taiUtcSeconds);
      if(!paired)continue;
      latency[@"video_ms"]=@(*paired);latency[@"videoState"]=@"estimated";
      latency[@"videoSource"]=@"paired-audio-timecode-tai/device-utc";
      latency[@"eventSeq"]=@(it->seq);latency[@"referenceAgeMs"]=@(now-packet.time_ms);break;
     }
    }
    latency[@"source"]=[latency[@"videoSource"] isEqual:@"paired-audio-timecode-tai/device-utc"]?@"paired-audio-timecode-tai/device-utc":latency[@"video_ms"]?@"live-qr-and-audio-timecode-tai/device-utc":@"audio-timecode-tai/device-utc";
   }else{
    latency[@"audioState"]=packet.live?@"waiting_for_device_clock":@"file_signal";
    if(!latency[@"video_ms"])latency[@"state"]=latency[@"audioState"];
   }
  }
 }
 result[@"latency"]=latency;
#if DEBUG || AVSYNC_DIAGNOSTIC_CAPTURE
 if(s.diagnostic)result[@"diagnosticCapture"]=s.diagnostic->status();
#endif
 result[@"diagnosticVersion"]=@3;result[@"recentLocatedFrames"]=@(recentLocated);result[@"recentValidFrames"]=@(recentValid);
 result[@"videoEvents"]=@(report.video_events);result[@"audioSamples"]=@(report.audio_samples);result[@"audioPeakDbfs"]=@(report.audio_peak_dbfs);
 result[@"audioHits"]=@(report.audio_hits.size());result[@"chirps"]=chirps;result[@"bestCorrelation"]=@(bestCorrelation);
 result[@"phaseRejectedFrames"]=@(report.phase_rejected_frames);result[@"sequenceRejectedFrames"]=@(report.sequence_rejected_frames);result[@"timingResets"]=@(report.timing_resets);
 result[@"pendingFrames"]=@(report.pending_frames);
 if(report.decoded_seq)result[@"decodedSeq"]=@(*report.decoded_seq);if(report.locked_seq)result[@"lockedSeq"]=@(*report.locked_seq);
 if(report.last_valid_frame_ms>0)result[@"lastValidFrameAgeMs"]=@(now-report.last_valid_frame_ms);
 if(report.last_video_event_ms>0)result[@"lastVideoEventAgeMs"]=@(now-report.last_video_event_ms);
 if(!offsets.empty())std::sort(offsets.begin(),offsets.end());
 double spread=offsets.empty()?0:offsets.back()-offsets.front();
 const double freshPeriod=s.qr?1000.0/std::max(1,s.qr->rate):500.0;
 const bool stable=recentLocated>0 && recentValid>0 && now-report.last_video_event_ms<=freshPeriod+300 && offsets.size()>=3 && spread<=std::max(50.0,3000.0/std::max(1.0,fps));
 result[@"spread_ms"]=@(spread);
 if(stable){
  std::sort(offsets.begin(),offsets.end());result[@"kind"]=@"reading";result[@"offset_ms"]=@(offsets[offsets.size()/2]);result[@"spread_ms"]=@(offsets.back()-offsets.front());result[@"quality"]=@"degraded";
 }
 NSString *stage=stable?@"measuring":recentLocated==0?@"picture_missing":recentValid==0?@"code_unreadable":report.video_events==0||now-report.last_video_event_ms>1500?@"video_tracking":report.audio_samples==0?@"audio_missing":report.audio_peak_dbfs < -60?@"audio_silent":report.audio_hits.empty()?@"chirps_missing":offsets.empty()?@"pairing":@"inconsistent_pairs";
 result[@"stage"]=stage;
 if(!stable){
  if([stage isEqual:@"picture_missing"])result[@"message"]=@"No test picture detected now. Keep all four corners visible.";
  else if([stage isEqual:@"code_unreadable"])result[@"message"]=@"Picture found now, but its event code is unreadable.";
  else if([stage isEqual:@"video_tracking"])result[@"message"]=@"Picture code decoded, but video event tracking has not locked.";
  else if([stage isEqual:@"audio_missing"])result[@"message"]=@"No microphone samples received.";
  else if([stage isEqual:@"audio_silent"])result[@"message"]=@"Video events found, but microphone audio is silent.";
  else if([stage isEqual:@"chirps_missing"])result[@"message"]=@"Video events found; audio received, but no test chirps recognized.";
  else if([stage isEqual:@"pairing"])result[@"message"]=@"Video and chirps detected, but event identities do not pair.";
  else result[@"message"]=@"Waiting for at least three consistent AV pairs; no measurement published.";
 }
 s.session->trim_before(now-12000);
 return result;
}
@end
