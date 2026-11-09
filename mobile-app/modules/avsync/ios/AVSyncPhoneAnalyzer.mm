#import "AVSyncPhoneAnalyzer.h"
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>
#include "avsync/phone_analyzer.hpp"
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
NSData *wav(const std::vector<float>& samples, int rate) {
 uint32_t length=(uint32_t)samples.size()*2,size=36+length,fmt=16,bytes=rate*2;
 uint16_t pcm=1,channels=1,align=2,bits=16;
 NSMutableData *data=[NSMutableData data];
 auto add=[&](const void*p,size_t n){[data appendBytes:p length:n];};
 add("RIFF",4);add(&size,4);add("WAVEfmt ",8);add(&fmt,4);add(&pcm,2);add(&channels,2);add(&rate,4);add(&bytes,4);add(&align,2);add(&bits,2);add("data",4);add(&length,4);
 for(float x:samples){int16_t value=(int16_t)std::lround(std::clamp(x,-1.f,1.f)*32767);add(&value,2);}
 return data;
}
NSString *jpeg(const avsync::PhoneAnalyzer::CalibrationFrame& f, NSString *path) {
 NSData *data=[NSData dataWithBytes:f.luma.data() length:f.luma.size()];
 CGDataProviderRef provider=CGDataProviderCreateWithCFData((__bridge CFDataRef)data);
 CGColorSpaceRef space=CGColorSpaceCreateDeviceGray();
 CGImageRef image=CGImageCreate(f.width,f.height,8,8,f.width,space,kCGImageAlphaNone,provider,nil,false,kCGRenderingIntentDefault);
 BOOL ok=image&&[UIImageJPEGRepresentation([UIImage imageWithCGImage:image],0.85) writeToFile:path atomically:YES];
 if(image)CGImageRelease(image);
 CGColorSpaceRelease(space);CGDataProviderRelease(provider);
 return ok?[NSURL fileURLWithPath:path].absoluteString:nil;
}
}

@implementation AVSyncPhoneAnalyzer { std::unique_ptr<avsync::PhoneAnalyzer> core; int width, height; }
- (instancetype)init { if((self=[super init])){core=std::make_unique<avsync::PhoneAnalyzer>();} return self; }
- (void)reset { core->reset(); }
- (void)beginCalibration { core->begin_calibration(); }
- (void)beginCalibrator:(NSString *)mode { core->begin_calibrator([mode isEqualToString:@"display"]?avsync::PhoneAnalyzer::CalibratorMode::Display:avsync::PhoneAnalyzer::CalibratorMode::Camera); }
- (void)setTaiUtcOffset:(double)seconds { core->set_tai_utc_seconds(seconds); }
- (void)setDeviceUtcOffset:(double)offset { core->set_device_utc_offset(offset); }
- (void)observeQRDetection:(NSDictionary *)detection timeMs:(double)timeMs {
 NSString *text=detection[@"payload"];NSArray *points=detection[@"points"];
 if(![text isKindOfClass:NSString.class]||points.count!=4)return;
 std::array<double,4> x{},y{};
 for(int i=0;i<4;++i){x[i]=[points[i][@"x"] doubleValue];y[i]=[points[i][@"y"] doubleValue];}
 core->observe_qr(text.UTF8String,x,y,timeMs);
}
- (BOOL)qrScanSuggestedAt:(double)timeMs { return core->qr_scan_suggested(timeMs); }
- (void)pushVideo:(CMSampleBufferRef)sample exposureMs:(double)exposureMs {
 const double time=CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sample))*1000;
 CVPixelBufferRef pixel=CMSampleBufferGetImageBuffer(sample);
 if(!std::isfinite(time)||!pixel||CVPixelBufferLockBaseAddress(pixel,kCVPixelBufferLock_ReadOnly)!=kCVReturnSuccess)return;
 width=(int)CVPixelBufferGetWidthOfPlane(pixel,0);height=(int)CVPixelBufferGetHeightOfPlane(pixel,0);
 core->push_video((const uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pixel,0),width,height,(int)CVPixelBufferGetBytesPerRowOfPlane(pixel,0),time,exposureMs,0);
 CVPixelBufferUnlockBaseAddress(pixel,kCVPixelBufferLock_ReadOnly);
}
- (void)pushAudio:(CMSampleBufferRef)sample {
 const double time=CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sample))*1000;
 const AudioStreamBasicDescription *format=CMAudioFormatDescriptionGetStreamBasicDescription(CMSampleBufferGetFormatDescription(sample));
 CMBlockBufferRef block=CMSampleBufferGetDataBuffer(sample);
 if(!std::isfinite(time)||!format||format->mFormatID!=kAudioFormatLinearPCM||!block)return;
 const size_t size=CMBlockBufferGetDataLength(block),count=CMSampleBufferGetNumSamples(sample);
 std::vector<uint8_t> bytes(size);
 if(CMBlockBufferCopyDataBytes(block,0,size,bytes.data())!=kCMBlockBufferNoErr)return;
 const int channels=(int)format->mChannelsPerFrame,bits=(int)format->mBitsPerChannel;
 const bool floating=format->mFormatFlags&kAudioFormatFlagIsFloat;
 const int stride=(format->mFormatFlags&kAudioFormatFlagIsNonInterleaved)?1:channels;
 if(channels<=0||(bits!=16&&bits!=32)||size<count*stride*(bits/8))return;
 // First channel only, as float.
 std::vector<float> mono(count);
 for(size_t i=0;i<count;++i){
  const size_t at=i*stride;
  mono[i]=floating&&bits==32?((const float*)bytes.data())[at]:bits==16?((const int16_t*)bytes.data())[at]/32768.f:((const int32_t*)bytes.data())[at]/2147483648.f;
 }
 core->push_audio(mono.data(),mono.size(),time,(int)format->mSampleRate);
}
- (BOOL)reportDue { return core->report_due(); }
- (AVSyncPhoneAnalyzer *)snapshotForPoll {
 AVSyncPhoneAnalyzer *copy=[AVSyncPhoneAnalyzer new];
 copy->core=std::make_unique<avsync::PhoneAnalyzer>(core->snapshot_for_poll());
 return copy;
}
- (NSDictionary *)poll {
 const auto text=core->poll();
 if(text.empty())return nil;
 NSData *data=[NSData dataWithBytes:text.data() length:text.size()];
 return [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
}
- (void)mergeAudio:(AVSyncPhoneAnalyzer *)analyzed { if(analyzed)core->merge_audio(*analyzed->core); }
- (NSDictionary *)pollCalibration {
 if(core->calibrator_active()){
  const auto text=core->poll_calibrator();
  if(text.empty())return nil;
  NSDictionary *report=[NSJSONSerialization JSONObjectWithData:[NSData dataWithBytes:text.data() length:text.size()] options:0 error:nil];
  if(![report[@"kind"] isEqual:@"calibrator-progress"])core->reset(); // done: live analysis resumes
  return report;
 }
 if(!core->calibrating())return nil;
 if(auto error=core->calibration_error()){NSString *message=[NSString stringWithUTF8String:error->c_str()];core->reset();return @{@"kind":@"error",@"message":message};}
 auto take=core->take_calibration();
 if(!take)return nil;
 core->reset(); // Live analysis resumes while the take is reviewed.
 NSString *dir=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
 [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
 NSMutableArray *frames=[NSMutableArray array];
 for(const auto& f:take->frames){
  NSString *uri=jpeg(f,[dir stringByAppendingPathComponent:[NSString stringWithFormat:@"%lu.jpg",(unsigned long)frames.count]]);
  if(!uri)return @{@"kind":@"error",@"message":@"Could not save calibration frames. Check free storage and try again."};
  [frames addObject:@{@"timeMs":@(f.time_ms),@"uri":uri}];
 }
 NSString *path=[dir stringByAppendingPathComponent:@"sound.wav"];
 if(![wav(take->audio,take->sample_rate) writeToFile:path atomically:YES])return @{@"kind":@"error",@"message":@"Could not save calibration sound. Check free storage and try again."};
 // 256 peaks for the waveform view.
 NSMutableArray *wave=[NSMutableArray array];
 const size_t bucket=std::max<size_t>(1,take->audio.size()/256);
 for(size_t i=0;i<take->audio.size();i+=bucket){float peak=0;for(size_t j=i;j<std::min(take->audio.size(),i+bucket);++j)peak=std::max(peak,std::abs(take->audio[j]));[wave addObject:@(peak)];}
 const double audioMs=take->audio.size()*1000.0/take->sample_rate;
 return @{@"kind":@"calibration",@"frames":frames,@"spikeTimeMs":@(take->spike_ms),@"fps":@(take->fps),@"waveform":wave,
          @"audioStartMs":@(take->audio_start_ms),@"waveformStartMs":@(take->audio_start_ms),@"waveformBinDurationMs":@(bucket*1000.0/take->sample_rate),
          @"waveformDurationMs":@(audioMs),@"durationMs":@(audioMs),@"uri":[NSURL fileURLWithPath:path].absoluteString};
}
- (NSDictionary *)pictureObservation {
 std::array<double,4> x{},y{};
 if(!core->picture(x,y))return @{@"found":@NO};
 NSMutableArray *points=[NSMutableArray array];
 for(int i=0;i<4;++i)[points addObject:@{@"x":@(x[i]),@"y":@(y[i])}];
 return @{@"found":@YES,@"codeDecoded":@YES,@"points":points,@"width":@(width),@"height":@(height)};
}
@end
