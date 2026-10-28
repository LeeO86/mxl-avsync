#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface AVSyncGenerator : NSObject
- (instancetype)initWithTestID:(NSString *)testID rate:(NSInteger)rate epochTaiNs:(int64_t)epochTaiNs epochHost:(double)epochHost NS_SWIFT_NAME(init(testID:rate:epochTaiNs:epochHost:));
- (void)setSourceName:(NSString *)name subtitle:(NSString *)subtitle image:(nullable UIImage *)image NS_SWIFT_NAME(setSource(name:subtitle:image:));
- (NSDictionary *)geometry;
- (NSDictionary *)frameAt:(double)targetHost NS_SWIFT_NAME(frame(at:));
- (double)hostTimeForSequence:(uint64_t)sequence NS_SWIFT_NAME(hostTime(sequence:));
- (NSData *)audioForSequence:(uint64_t)sequence NS_SWIFT_NAME(audio(sequence:));
- (UIImage *)imageForSequence:(uint64_t)sequence NS_SWIFT_NAME(image(sequence:));
@end

NS_ASSUME_NONNULL_END
