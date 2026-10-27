#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>

@interface AVSyncQRCodeReader : NSObject
+ (NSDictionary *)readPixelBuffer:(CVPixelBufferRef)pixel NS_SWIFT_NAME(read(_:));
@end
