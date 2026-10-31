#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import "AVSyncStreamAnalyzer.h"
#import "AVSyncQRCodeReader.h"
#include "avsync/avsync.hpp"
#include <cassert>
int main(int argc,char**argv){@autoreleasepool{
 NSURL*url=[NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]];
 AVAsset*asset=[AVURLAsset URLAssetWithURL:url options:nil];
 AVAssetReader*reader=[[AVAssetReader alloc]initWithAsset:asset error:nil];
 AVAssetReaderTrackOutput*video=[[AVAssetReaderTrackOutput alloc]initWithTrack:[asset tracksWithMediaType:AVMediaTypeVideo].firstObject outputSettings:@{(id)kCVPixelBufferPixelFormatTypeKey:@(kCVPixelFormatType_420YpCbCr8BiPlanarFullRange)}];
 AVAssetReaderAudioMixOutput*audio=[[AVAssetReaderAudioMixOutput alloc]initWithAudioTracks:[asset tracksWithMediaType:AVMediaTypeAudio] audioSettings:@{AVFormatIDKey:@(kAudioFormatLinearPCM),AVSampleRateKey:@48000,AVNumberOfChannelsKey:@1,AVLinearPCMBitDepthKey:@16,AVLinearPCMIsFloatKey:@NO,AVLinearPCMIsNonInterleaved:@NO}];
 [reader addOutput:video];[reader addOutput:audio];[reader startReading];
 AVSyncStreamAnalyzer*processor=[AVSyncStreamAnalyzer new];if(argc>2)[processor beginCalibration];
 NSDictionary *environment=NSProcessInfo.processInfo.environment;
 NSString *testQR=environment[@"AVSYNC_TEST_QR"];
 const bool snapshots=environment[@"AVSYNC_TEST_ASYNC_SNAPSHOTS"]!=nil;
 AVSyncStreamAnalyzer *retainedSnapshot=nil;NSDictionary *retainedPicture=nil;
 if(environment[@"AVSYNC_TEST_WALL_OFFSET_MS"])[processor setClockAnchor:0 unixTimeMs:[environment[@"AVSYNC_TEST_WALL_OFFSET_MS"] doubleValue]];
 CMSampleBufferRef v=[video copyNextSampleBuffer],a=[audio copyNextSampleBuffer];
 NSMutableArray*reports=[NSMutableArray array];double lastQR=-1e30;
 while(v||a){bool chooseVideo=v&&(!a||CMTimeCompare(CMSampleBufferGetPresentationTimeStamp(v),CMSampleBufferGetPresentationTimeStamp(a))<=0);
  const double currentMs=CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(chooseVideo?v:a))*1000;
  if(chooseVideo){
   if(environment[@"AVSYNC_TEST_SCAN_QR"]&&currentMs-lastQR>=100){
    lastQR=currentMs;NSDictionary *qr=[AVSyncQRCodeReader readPixelBuffer:CMSampleBufferGetImageBuffer(v)];
    for(NSDictionary *detection in qr[@"detections"])[processor observeQRDetection:detection timeMs:currentMs];
   }
   [processor pushVideo:v];CFRelease(v);v=[video copyNextSampleBuffer];}else{[processor pushAudio:a];CFRelease(a);a=[audio copyNextSampleBuffer];}
  if(testQR){
   if(environment[@"AVSYNC_TEST_ADVANCE_QR"]){
    auto qr=avsync::parse_qr_payload(testQR.UTF8String);
    if(qr){
     auto format=avsync::parse_video_format(qr->format);
     const auto period=avsync::plan_period(*format,qr->rate);
     const int64_t delta=std::max<int64_t>(0,int64_t(std::floor((currentMs-1000)/period.period_ms)));
     qr->k+=delta;qr->tk+=delta*avsync::index_to_ns(format->grain_rate(),period.grains);
     [processor observeQR:[NSString stringWithUTF8String:avsync::qr_payload_json(*qr).c_str()] timeMs:currentMs];
    }
   }else [processor observeQR:testQR timeMs:0];
  }
  NSDictionary*report=nil;
  if(snapshots){
   AVSyncStreamAnalyzer *snapshot=[processor liveSnapshotForPolling];
   if(snapshot){report=[snapshot poll];assert([processor acceptLiveAnalysis:snapshot]);if(!retainedSnapshot){retainedSnapshot=snapshot;retainedPicture=[snapshot pictureObservation];}}
  }else report=[processor poll];
  if(report){[reports addObject:report];if([report[@"kind"] isEqual:@"calibration"])break;}
 }
 if(snapshots){assert(retainedSnapshot);if(testQR){auto other=avsync::parse_qr_payload(testQR.UTF8String);assert(other);other->source_id^=1;other->test_id="other-source";[processor observeQR:[NSString stringWithUTF8String:avsync::qr_payload_json(*other).c_str()] timeMs:999999];assert(![processor acceptLiveAnalysis:retainedSnapshot]);}[processor reset];assert([retainedPicture isEqual:[retainedSnapshot pictureObservation]]);}
 if(v)CFRelease(v);if(a)CFRelease(a);
 NSData*json=[NSJSONSerialization dataWithJSONObject:reports options:0 error:nil];puts([[NSString alloc]initWithData:json encoding:NSUTF8StringEncoding].UTF8String);
 return 0;
}}
