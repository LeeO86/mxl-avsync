#import <Foundation/Foundation.h>
#import <CoreMedia/CoreMedia.h>
@interface AVSyncStreamAnalyzer : NSObject
- (void)reset;
- (void)beginCalibration;
- (void)pushVideo:(CMSampleBufferRef)sample;
- (void)pushAudio:(CMSampleBufferRef)sample;
- (NSDictionary *)poll;
- (NSDictionary *)pictureObservation;
- (void)observeQR:(NSString *)text timeMs:(double)timeMs;
- (void)observeQRDetection:(NSDictionary *)detection timeMs:(double)timeMs;
- (void)setClockAnchor:(double)captureTimeMs unixTimeMs:(double)unixTimeMs;
- (void)setTaiUtcOffset:(double)seconds;
@end
