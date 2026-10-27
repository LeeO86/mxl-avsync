#import "AVSyncFileAnalyzer.h"
#import <AVFoundation/AVFoundation.h>
#import "AVSyncQRCodeReader.h"
#import <UIKit/UIKit.h>
#include "avsync/avsync.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

static NSDictionary *failure(NSString *message) { return @{ @"error": message }; }

@implementation AVSyncFileAnalyzer
+ (NSDictionary *)analyze:(NSString *)uri calibration:(BOOL)calibration {
 @try {
  NSURL *url = [NSURL URLWithString:uri];
  if (!url.isFileURL) return failure(@"Only a local recording can be analyzed.");
  AVURLAsset *asset = [AVURLAsset URLAssetWithURL:url options:@{AVURLAssetPreferPreciseDurationAndTimingKey:@YES}];
  AVAssetTrack *audioTrack = [asset tracksWithMediaType:AVMediaTypeAudio].firstObject;
  AVAssetTrack *videoTrack = [asset tracksWithMediaType:AVMediaTypeVideo].firstObject;
  if (!audioTrack) return failure(@"The recording has no audio. Allow microphone access and record again.");
  if (!videoTrack) return failure(@"The recording has no video.");
  double duration = CMTimeGetSeconds(asset.duration);
  if (!std::isfinite(duration) || duration <= 0 || duration > 16) return failure(@"Record between 1 and 15 seconds.");
  NSError *error = nil;
  AVAssetReader *reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
  AVAssetReaderAudioMixOutput *audioOut = [[AVAssetReaderAudioMixOutput alloc] initWithAudioTracks:@[audioTrack] audioSettings:@{
    AVFormatIDKey:@(kAudioFormatLinearPCM), AVSampleRateKey:@48000, AVNumberOfChannelsKey:@1,
    AVLinearPCMBitDepthKey:@32, AVLinearPCMIsFloatKey:@YES, AVLinearPCMIsNonInterleaved:@NO }];
  if (![reader canAddOutput:audioOut]) return failure(@"Cannot decode the recording audio.");
  [reader addOutput:audioOut];
  if (![reader startReading]) return failure(reader.error.localizedDescription ?: @"Cannot open audio.");
  avsync::PhoneSession session(2, avsync::GradeWindows{});
  std::vector<float> audio;
  double audioOrigin = 0;
  bool haveOrigin = false;
  CMSampleBufferRef sample;
  while ((sample = [audioOut copyNextSampleBuffer])) {
    double pts = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sample))*1000;
    if (!haveOrigin) { audioOrigin=pts; haveOrigin=true; }
    CMBlockBufferRef block=CMSampleBufferGetDataBuffer(sample);
    size_t size=block ? CMBlockBufferGetDataLength(block) : 0;
    std::vector<float> chunk(size/sizeof(float));
    if (size && CMBlockBufferCopyDataBytes(block,0,size,chunk.data()) == kCMBlockBufferNoErr) {
      size_t expected=(size_t)std::max(0.0,std::round((pts-audioOrigin)*48));
      if (expected>audio.size() && expected<48000*17) audio.resize(expected,0);
      audio.insert(audio.end(),chunk.begin(),chunk.end());
      if (!calibration) session.push_audio(chunk.data(),chunk.size(),pts,48000);
    }
    CFRelease(sample);
  }
  if (reader.status == AVAssetReaderStatusFailed) return failure(reader.error.localizedDescription ?: @"Audio decoding failed.");
  if (audio.empty()) return failure(@"No audio samples were decoded.");
  NSMutableArray *waveform=[NSMutableArray array];
  size_t bucket=std::max((size_t)1,audio.size()/96);
  for(size_t i=0;i<audio.size();i+=bucket) {
    float peak=0;
    for(size_t j=i;j<std::min(audio.size(),i+bucket);++j) peak=std::max(peak,std::abs(audio[j]));
    [waveform addObject:@(peak)];
  }
  auto peakIt=std::max_element(audio.begin(),audio.end(),[](float a,float b){return std::abs(a)<std::abs(b);});
  size_t spikeIndex=std::distance(audio.begin(),peakIt);
  double spikeMs=audioOrigin+spikeIndex/48.0;
  double energy=0; for(float x:audio) energy+=x*x;
  double rms=std::sqrt(energy/audio.size());
  if(calibration && (std::abs(*peakIt)<0.05 || std::abs(*peakIt)<rms*3))
    return failure(@"No clear sharp sound found. Record one clap or tap after a short quiet pause.");

  reader=[[AVAssetReader alloc] initWithAsset:asset error:&error];
  AVAssetReaderTrackOutput *videoOut=[[AVAssetReaderTrackOutput alloc] initWithTrack:videoTrack outputSettings:@{(id)kCVPixelBufferPixelFormatTypeKey:@(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange)}];
  if(![reader canAddOutput:videoOut]) return failure(@"Cannot decode the recording video.");
  [reader addOutput:videoOut];
  if(![reader startReading]) return failure(reader.error.localizedDescription ?: @"Cannot open video.");
  NSMutableArray *frames=[NSMutableArray array];
  AVAssetImageGenerator *images=[[AVAssetImageGenerator alloc] initWithAsset:asset];
  images.appliesPreferredTrackTransform=YES;
  images.maximumSize=CGSizeMake(640,640);
  images.requestedTimeToleranceBefore=kCMTimeZero;
  images.requestedTimeToleranceAfter=kCMTimeZero;
  NSString *directory=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
  if(calibration) [[NSFileManager defaultManager] createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nil];
  int decoded=0, decodedQR=0; double lastQR=-1e30; NSString *qrError=@"none";
  while((sample=[videoOut copyNextSampleBuffer])) {
   @autoreleasepool {
    CMTime time=CMSampleBufferGetPresentationTimeStamp(sample);
    double ms=CMTimeGetSeconds(time)*1000;
    CVPixelBufferRef pixel=CMSampleBufferGetImageBuffer(sample);
    if(pixel) {
      ++decoded;
      if(!calibration) {
        if(ms-lastQR>=500){
          lastQR=ms;
          NSDictionary *reading=[AVSyncQRCodeReader readPixelBuffer:pixel];qrError=reading[@"error"];
          for(NSString *text in reading[@"payloads"]){
            auto qr=avsync::parse_qr_payload(text.UTF8String);if(qr){session.observe_signal(*qr);++decodedQR;}
          }
        }
        CVPixelBufferLockBaseAddress(pixel,kCVPixelBufferLock_ReadOnly);
        int w=(int)CVPixelBufferGetWidthOfPlane(pixel,0),h=(int)CVPixelBufferGetHeightOfPlane(pixel,0);
        int stride=(int)CVPixelBufferGetBytesPerRowOfPlane(pixel,0);
        const uint8_t *base=(const uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pixel,0);
        // Bound detector work while preserving the test-picture geometry.
        CGAffineTransform transform=videoTrack.preferredTransform;
        CGRect bounds=CGRectApplyAffineTransform(CGRectMake(0,0,w,h),transform);
        CGAffineTransform inverse=CGAffineTransformInvert(transform);
        int uprightW=(int)std::round(std::abs(bounds.size.width)), uprightH=(int)std::round(std::abs(bounds.size.height));
        int step=std::max(1,(std::max(uprightW,uprightH)+959)/960), sw=uprightW/step, sh=uprightH/step;
        std::vector<uint16_t> luma(sw*sh);
        for(int y=0;y<sh;y++) for(int x=0;x<sw;x++) {
          CGPoint source=CGPointApplyAffineTransform(CGPointMake(x*step+CGRectGetMinX(bounds),y*step+CGRectGetMinY(bounds)),inverse);
          int sx=std::clamp((int)std::round(source.x),0,w-1),sy=std::clamp((int)std::round(source.y),0,h-1);
          luma[y*sw+x]=(uint16_t)base[sy*stride+sx]*4;
        }
        session.push_video(luma.data(),sw,sh,sw,ms,0);
        CVPixelBufferUnlockBaseAddress(pixel,kCVPixelBufferLock_ReadOnly);
      } else if(std::abs(ms-spikeMs)<=750) {
        CMTime actual;
        CGImageRef image=[images copyCGImageAtTime:time actualTime:&actual error:&error];
        if(image) {
          NSString *path=[directory stringByAppendingPathComponent:[NSString stringWithFormat:@"%d.jpg",decoded]];
          NSData *jpeg=UIImageJPEGRepresentation([UIImage imageWithCGImage:image],0.8);
          if([jpeg writeToFile:path atomically:YES]) [frames addObject:@{@"timeMs":@(CMTimeGetSeconds(actual)*1000),@"uri":[NSURL fileURLWithPath:path].absoluteString}];
          CGImageRelease(image);
        }
      }
    }
    CFRelease(sample);
   }
  }
  if(reader.status==AVAssetReaderStatusFailed) return failure(reader.error.localizedDescription ?: @"Video decoding failed.");
  double fps=videoTrack.nominalFrameRate;
  NSMutableDictionary *result=[@{@"waveform":waveform,@"spikeTimeMs":@(spikeMs),@"durationMs":@(duration*1000),@"fps":@(fps),@"decodedFrames":@(decoded),@"uri":uri} mutableCopy];
  if(calibration) {
    int before=0,after=0;
    for(NSDictionary*frame in frames){double t=[frame[@"timeMs"]doubleValue];if(t<spikeMs)++before;else if(t>spikeMs)++after;}
    if(before<2||after<2)return failure(@"Need camera frames BEFORE and AFTER the sound. Repeat and keep recording after the clap.");
    result[@"framesBefore"]=@(before);result[@"framesAfter"]=@(after);
    result[@"frames"]=frames;
    return result;
  }
  auto report=session.finish("PHONE");
  NSMutableArray *events=[NSMutableArray array];
  std::vector<double> offsets;
  for(const auto& event:report.events) {
    NSMutableDictionary *item=[@{@"event_id":[NSString stringWithUTF8String:event.event_id.c_str()],@"reason":[NSString stringWithUTF8String:event.reason.c_str()],@"video_time_ms":@(event.video_time_ms),@"audio_time_ms":@(event.audio_time_ms)} mutableCopy];
    if(event.av_offset_ms && std::isfinite(*event.av_offset_ms)) {item[@"offset_ms"]=@(*event.av_offset_ms);if(event.grade!="invalid") offsets.push_back(*event.av_offset_ms);}
    [events addObject:item];
  }
  if(offsets.empty()) return failure([NSString stringWithFormat:@"No matching AV events. Picture located in %d/%d frames; readable marker code in %d; %lu timecodes, %lu video events, %d QRs (QR error: %@).",session.located_frames(),decoded,session.valid_frames(),(unsigned long)report.audio_timecodes.size(),(unsigned long)report.video_events,decodedQR,qrError]);
  std::sort(offsets.begin(),offsets.end());
  double median=offsets[offsets.size()/2];
  result[@"offset_ms"]=@(median);
  result[@"events"]=events;
  result[@"eventCount"]=@(offsets.size());
  result[@"spread_ms"]=@(offsets.back()-offsets.front());
  result[@"quality"]=@"degraded";
  result[@"reference"]=@{@"type":@"none"};
  result[@"timingNote"]=@"Recorded media timestamps; rolling shutter and capture accuracy not validated.";
  return result;
 } @catch(NSException *exception) { return failure(exception.reason ?: @"Native analysis failed."); }
}
@end
