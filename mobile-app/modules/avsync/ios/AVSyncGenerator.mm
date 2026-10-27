#import "AVSyncGenerator.h"
#include "avsync/phone_generator.hpp"
#include "../../../../third_party/nayuki/qrcodegen.hpp"
#include <memory>
#include <algorithm>

@implementation AVSyncGenerator {
    std::unique_ptr<avsync::PhoneGeneratorPlan> _plan;
}
- (instancetype)initWithTestID:(NSString *)testID rate:(NSInteger)rate epochTaiNs:(int64_t)epoch epochHost:(double)host {
    if ((self = [super init])) _plan = std::make_unique<avsync::PhoneGeneratorPlan>(testID.UTF8String,int(rate),epoch,host);
    return self;
}
- (NSDictionary *)geometry {
    auto g=avsync::frame_geometry(1280,720);
    return @{ @"width":@1280, @"height":@720, @"markerX":@(g.marker_x), @"markerY":@(g.marker_y), @"markerSide":@(g.marker_side),
              @"stripX":@(g.strip_x), @"stripY":@(g.strip_y), @"stripWidth":@(g.strip_w), @"stripHeight":@(g.strip_h) };
}
- (NSDictionary *)frameAt:(double)target {
    const auto frame = _plan->frame_at(target);
    const auto period = _plan->period_frames();
    const uint64_t seq = 1 + frame / period;
    const int pos = int(frame % period);
    uint32_t gray=avsync::binary_to_gray(uint32_t(seq & 0xffffff));
    const uint32_t data=(gray<<4)|(pos&15);
    const uint32_t bits=(data<<4)|((__builtin_popcount(data)&1)<<3)|5;
    return @{ @"sequence":@(seq), @"position":@(pos), @"white":@(avsync::flash_white(pos,1000.0/60)), @"strip":@(bits) };
}
- (double)hostTimeForSequence:(uint64_t)seq { return _plan->host_seconds(seq); }
- (NSData *)audioForSequence:(uint64_t)seq {
    const auto samples=_plan->audio(seq);
    return [NSData dataWithBytes:samples.data() length:samples.size()*sizeof(float)];
}
- (UIImage *)imageForSequence:(uint64_t)seq {
    const auto qr=_plan->qr(seq);
    std::vector<uint16_t> luma(1280*720);
    avsync::LumaView view{luma.data(),1280,720,1280};
    avsync::fill_luma(view,avsync::kYBackground); avsync::draw_static_layer(view);
    // Initial dynamic pixels are overwritten by the native timing layers.
    avsync::draw_timing(view,seq,0,_plan->period_frames(),false);
    const auto code=qrcodegen::QrCode::encodeText(avsync::qr_payload_json(qr).c_str(),qrcodegen::QrCode::Ecc::MEDIUM);
    const auto g=avsync::frame_geometry(1280,720);
    const int span=code.getSize()+8, scale=std::max(1,g.qr_size/span);
    for(int y=0;y<span*scale;++y) for(int x=0;x<span*scale;++x) {
        const int mx=x/scale-4,my=y/scale-4;
        luma[(g.qr_y+y)*1280+g.qr_x+x]=(mx>=0&&my>=0&&mx<code.getSize()&&my<code.getSize()&&code.getModule(mx,my))?avsync::kYBlack:avsync::kYWhite;
    }
    NSMutableData *gray=[NSMutableData dataWithLength:luma.size()];
    auto *dst=(uint8_t *)gray.mutableBytes;
    for(size_t i=0;i<luma.size();++i) dst[i]=uint8_t(std::clamp((int(luma[i])-64)*255/876,0,255));
    CGDataProviderRef provider=CGDataProviderCreateWithCFData((__bridge CFDataRef)gray);
    CGColorSpaceRef space=CGColorSpaceCreateDeviceGray();
    CGImageRef image=CGImageCreate(1280,720,8,8,1280,space,kCGImageAlphaNone,provider,nullptr,false,kCGRenderingIntentDefault);
    UIImage *base=[UIImage imageWithCGImage:image];
    CGImageRelease(image); CGColorSpaceRelease(space); CGDataProviderRelease(provider);
    UIGraphicsImageRendererFormat *format=[UIGraphicsImageRendererFormat defaultFormat]; format.scale=1; format.opaque=YES;
    UIGraphicsImageRenderer *renderer=[[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake(1280,720) format:format];
    return [renderer imageWithActions:^(UIGraphicsImageRendererContext *context) {
        [base drawAtPoint:CGPointZero];
        void (^label)(NSString *,CGFloat,CGFloat,CGFloat)=^(NSString *text,CGFloat x,CGFloat y,CGFloat size) {
            [text drawAtPoint:CGPointMake(x,y) withAttributes:@{NSFontAttributeName:[UIFont systemFontOfSize:size weight:UIFontWeightMedium],NSForegroundColorAttributeName:UIColor.whiteColor}];
        };
        label(@"TEST SIGNAL — mxl-avsync",365,20,30);
        label(@"iPhone source · scheduled output",365,68,22);
        label([NSString stringWithFormat:@"1200 baud · %d chirp%@/s",qr.rate,qr.rate==1?@"":@"s"],850,235,24);
        label(@"White flash = chirp onset",850,275,21);
        label([NSString stringWithFormat:@"Event %llu",(unsigned long long)seq],850,325,26);
        label(@"AV offset includes phone output",365,590,22);
        label(@"ESTIMATED LATENCY · clock / display / speaker uncalibrated",220,635,21);
    }];
}
@end
