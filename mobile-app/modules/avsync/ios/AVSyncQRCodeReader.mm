#import "AVSyncQRCodeReader.h"
#import <Vision/Vision.h>
#import <TargetConditionals.h>
#include <chrono>

@implementation AVSyncQRCodeReader
+ (NSDictionary *)readPixelBuffer:(CVPixelBufferRef)pixel {
    NSString *lastError=@"none";
    // Keep a compatible QR reader when the newest Vision revision cannot read
    // this valid signal. Simulator inference devices also differ from iPhone.
#if TARGET_OS_SIMULATOR
    NSArray<NSNumber *> *revisions=@[@2];
#else
    NSArray<NSNumber *> *revisions=VNDetectBarcodesRequest.defaultRevision==2?@[@2]:@[@(VNDetectBarcodesRequest.defaultRevision),@2];
#endif
    NSMutableArray *timings=[NSMutableArray array];
    for(NSNumber *revision in revisions){
        const auto started=std::chrono::steady_clock::now();
        VNDetectBarcodesRequest *request=[VNDetectBarcodesRequest new];
        request.revision=revision.unsignedIntegerValue;
        request.symbologies=@[VNBarcodeSymbologyQR];
        if(request.revision==2)request.usesCPUOnly=YES;
        VNImageRequestHandler *handler=[[VNImageRequestHandler alloc]initWithCVPixelBuffer:pixel options:@{}];
        NSError *error=nil;
        const BOOL success=[handler performRequests:@[request] error:&error];
        [timings addObject:@(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count())];
        if(!success){
            lastError=error.localizedDescription?:@"Vision QR failed";continue;
        }
        NSMutableArray *payloads=[NSMutableArray array],*detections=[NSMutableArray array];
        for(VNBarcodeObservation *code in request.results) {
            NSString *text=code.payloadStringValue;
            if(![text containsString:@"mxl-avsync/1"]&&![text hasPrefix:@"AVS2:"])continue;
            [payloads addObject:text];
            CGPoint p[]={code.topLeft,code.topRight,code.bottomRight,code.bottomLeft};
            NSMutableArray *points=[NSMutableArray array];
            for(int i=0;i<4;++i)[points addObject:@{@"x":@(p[i].x),@"y":@(1-p[i].y)}];
            [detections addObject:@{@"payload":text,@"points":points}];
        }
        if(payloads.count)return @{@"payloads":payloads,@"detections":detections,@"revision":revision,@"timingsMs":timings,@"error":@"none"};
    }
    return @{@"payloads":@[],@"timingsMs":timings,@"error":lastError};
}
@end
