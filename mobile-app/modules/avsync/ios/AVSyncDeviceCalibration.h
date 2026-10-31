#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
// Experimental maths only. Does not acquire recordings or claim device calibration.
@interface AVSyncDeviceCalibration : NSObject
+ (NSDictionary *)solve:(NSDictionary *)input;
@end
NS_ASSUME_NONNULL_END
