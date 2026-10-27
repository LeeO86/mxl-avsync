#import "AVSyncQRCodeReader.h"
#import <Vision/Vision.h>
#import <TargetConditionals.h>

@implementation AVSyncQRCodeReader
+ (NSDictionary *)readPixelBuffer:(CVPixelBufferRef)pixel {
    NSString *lastError=@"none";
    // Keep a compatible QR reader when the newest Vision revision cannot read
    // this valid signal. Simulator inference devices also differ from iPhone.
#if TARGET_OS_SIMULATOR
    NSArray<NSNumber *> *revisions=@[@2];
#else
    NSArray<NSNumber *> *revisions=@[@(VNDetectBarcodesRequest.defaultRevision),@2];
#endif
    for(NSNumber *revision in revisions){
        VNDetectBarcodesRequest *request=[VNDetectBarcodesRequest new];
        request.revision=revision.unsignedIntegerValue;
        request.symbologies=@[VNBarcodeSymbologyQR];
        if(request.revision==2)request.usesCPUOnly=YES;
        VNImageRequestHandler *handler=[[VNImageRequestHandler alloc]initWithCVPixelBuffer:pixel options:@{}];
        NSError *error=nil;
        if(![handler performRequests:@[request] error:&error]){
            lastError=error.localizedDescription?:@"Vision QR failed";continue;
        }
        NSMutableArray *payloads=[NSMutableArray array];
        for(VNBarcodeObservation *code in request.results)
            if([code.payloadStringValue containsString:@"mxl-avsync/1"])[payloads addObject:code.payloadStringValue];
        if(payloads.count)return @{@"payloads":payloads,@"revision":revision,@"error":@"none"};
    }
    return @{@"payloads":@[],@"error":lastError};
}
@end
