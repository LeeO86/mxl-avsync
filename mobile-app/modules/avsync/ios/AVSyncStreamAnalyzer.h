#import <Foundation/Foundation.h>
#import <CoreMedia/CoreMedia.h>
@interface AVSyncStreamAnalyzer : NSObject
- (void)reset;
- (void)beginCalibration;
- (void)pushVideo:(CMSampleBufferRef)sample;
- (void)pushAudio:(CMSampleBufferRef)sample;
- (NSDictionary *)poll;
// Capture queue only: calibration stays synchronous; live DSP runs on an immutable copy.
- (NSDictionary *)pollCalibration;
- (AVSyncStreamAnalyzer *)liveSnapshotForPolling;
- (BOOL)acceptLiveAnalysis:(AVSyncStreamAnalyzer *)snapshot;
- (NSDictionary *)pictureObservation;
- (void)observeQR:(NSString *)text timeMs:(double)timeMs;
- (void)observeQRDetection:(NSDictionary *)detection timeMs:(double)timeMs;
- (void)setClockAnchor:(double)captureTimeMs unixTimeMs:(double)unixTimeMs;
- (void)setTaiUtcOffset:(double)seconds;
@end
