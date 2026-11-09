#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface AVSyncGenerator : NSObject
+ (NSString *)testIDWithPrefix:(NSString *)prefix installID:(NSString *)installID version:(NSString *)version settings:(NSArray<NSString *> *)settings NS_SWIFT_NAME(testID(prefix:installID:version:settings:));
- (instancetype)initWithTestID:(NSString *)testID rate:(NSInteger)rate epochTaiNs:(int64_t)epochTaiNs epochHost:(double)epochHost NS_SWIFT_NAME(init(testID:rate:epochTaiNs:epochHost:));
- (void)setSourceName:(NSString *)name subtitle:(NSString *)subtitle image:(nullable UIImage *)image NS_SWIFT_NAME(setSource(name:subtitle:image:));
- (NSDictionary *)geometry;
- (UIImage *)syncForeground;
- (NSDictionary *)frameAt:(double)targetHost NS_SWIFT_NAME(frame(at:));
- (double)hostTimeForSequence:(uint64_t)sequence NS_SWIFT_NAME(hostTime(sequence:));
- (NSData *)audioForSequence:(uint64_t)sequence NS_SWIFT_NAME(audio(sequence:));
- (UIImage *)imageForSequence:(uint64_t)sequence NS_SWIFT_NAME(image(sequence:));
// AVS4 sender calibration (this phone's screen vs speaker); method none until set.
- (void)setSenderCalibrationMethod:(NSString *)method offsetMs:(double)offsetMs uncertaintyMs:(double)uncertaintyMs day:(NSInteger)day NS_SWIFT_NAME(setSenderCalibration(method:offsetMs:uncertaintyMs:day:));

// Calibration pattern (docs/calibrator.md display mode; core/include/avsync/calibrator.hpp).
+ (BOOL)calibrationLeftWhiteAtFrame:(uint64_t)frame fps:(NSInteger)fps NS_SWIFT_NAME(calibrationLeftWhite(frame:fps:));
+ (uint64_t)calibrationSwapFrame:(uint64_t)swap fps:(NSInteger)fps NS_SWIFT_NAME(calibrationSwapFrame(swap:fps:));
+ (NSData *)calibrationBeepWithSampleRate:(double)sampleRate NS_SWIFT_NAME(calibrationBeep(sampleRate:));
// Clicks and beeps in mono float samples: pairs, clicks and, with enough pairs, the result.
+ (NSDictionary<NSString *, id> *)displayCalibrationForSamples:(NSData *)samples sampleRate:(double)sampleRate minimumPairs:(NSInteger)minimumPairs NS_SWIFT_NAME(displayCalibration(samples:sampleRate:minimumPairs:));
@end

NS_ASSUME_NONNULL_END
