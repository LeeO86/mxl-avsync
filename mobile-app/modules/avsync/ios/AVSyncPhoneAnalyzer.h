#import <Foundation/Foundation.h>
#import <CoreMedia/CoreMedia.h>

// Objective-C face of avsync::PhoneAnalyzer (core/include/avsync/phone_analyzer.hpp).
// Use from one queue; a snapshot may be polled on another.
NS_ASSUME_NONNULL_BEGIN
@interface AVSyncPhoneAnalyzer : NSObject
- (void)reset;
- (void)beginCalibration;
- (void)setTaiUtcOffset:(double)seconds;
- (void)setDeviceUtcOffset:(double)unixMinusCaptureMs;
- (void)observeQRDetection:(NSDictionary *)detection timeMs:(double)timeMs;
- (BOOL)qrScanSuggestedAt:(double)timeMs NS_SWIFT_NAME(qrScanSuggested(at:));
- (void)pushVideo:(CMSampleBufferRef)sample exposureMs:(double)exposureMs;
- (void)pushAudio:(CMSampleBufferRef)sample;
- (BOOL)reportDue;
- (AVSyncPhoneAnalyzer *)snapshotForPoll;
- (nullable NSDictionary *)poll;
- (void)mergeAudio:(AVSyncPhoneAnalyzer *)analyzed;
// nil while a take runs; then once an error report or a calibration report with written files.
- (nullable NSDictionary *)pollCalibration;
- (NSDictionary *)pictureObservation;
@end
NS_ASSUME_NONNULL_END
