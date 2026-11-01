#import <Foundation/Foundation.h>
#import <CoreMedia/CoreMedia.h>
typedef struct { double lockMs, processMs, unlockMs; int path; } AVSyncVideoWork;
@interface AVSyncStreamAnalyzer : NSObject
- (void)reset;
- (void)beginCalibration;
- (AVSyncVideoWork)pushVideo:(CMSampleBufferRef)sample;
- (BOOL)qrScanSuggestedAt:(double)timeMs NS_SWIFT_NAME(qrScanSuggested(at:));
- (void)setSparseAcquisition:(BOOL)enabled;
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
