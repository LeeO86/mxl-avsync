#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
@interface AVSyncFileAnalyzer : NSObject
+ (NSDictionary *)analyze:(NSString *)uri calibration:(BOOL)calibration;
@end
NS_ASSUME_NONNULL_END
