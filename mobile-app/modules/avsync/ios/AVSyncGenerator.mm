#import "AVSyncGenerator.h"
#include "avsync/phone_generator.hpp"
#include "../../../../generator/picture.hpp"
#include <memory>
#include <algorithm>

namespace {
void rgb(uint16_t y,uint16_t cb,uint16_t cr,uint8_t *out) {
    const double l=(double(y)-64)/876,u=(double(cb)-512)/896,v=(double(cr)-512)/896;
    out[0]=uint8_t(std::lround(std::clamp(l+1.5748*v,0.0,1.0)*255));
    out[1]=uint8_t(std::lround(std::clamp(l-.187324*u-.468124*v,0.0,1.0)*255));
    out[2]=uint8_t(std::lround(std::clamp(l+1.8556*u,0.0,1.0)*255));out[3]=255;
}
UIImage *image_from_frame(const avsync::Yuv422p10& frame,bool white_only=false) {
    NSMutableData *rgba=[NSMutableData dataWithLength:size_t(frame.width)*frame.height*4];auto *dst=(uint8_t*)rgba.mutableBytes;
    for(int y=0;y<frame.height;++y)for(int x=0;x<frame.width;++x) {
        const size_t ci=size_t(y)*(frame.width/2)+x/2;
        auto *pixel=dst+(size_t(y)*frame.width+x)*4;
        if(white_only) { const uint8_t value=frame.y[size_t(y)*frame.width+x]==avsync::kYWhite?255:0;pixel[0]=pixel[1]=pixel[2]=pixel[3]=value; }
        else rgb(frame.y[size_t(y)*frame.width+x],frame.cb[ci],frame.cr[ci],pixel);
    }
    CGDataProviderRef provider=CGDataProviderCreateWithCFData((__bridge CFDataRef)rgba);
    CGColorSpaceRef space=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGImageRef image=CGImageCreate(frame.width,frame.height,8,32,frame.width*4,space,kCGImageAlphaPremultipliedLast,provider,nullptr,false,kCGRenderingIntentDefault);
    UIImage *result=[UIImage imageWithCGImage:image];CGImageRelease(image);CGColorSpaceRelease(space);CGDataProviderRelease(provider);return result;
}
void make_still(UIImage *image,avsync::SourceStill& still) {
    if(!image.CGImage)return;
    const double factor=std::min(1.0,512.0/std::max(image.size.width,image.size.height));
    const int w=std::max(1,int(std::lround(image.size.width*factor))),h=std::max(1,int(std::lround(image.size.height*factor)));
    UIGraphicsImageRendererFormat *format=[UIGraphicsImageRendererFormat defaultFormat];format.scale=1;
    UIImage *normalized=[[[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake(w,h) format:format] imageWithActions:^(UIGraphicsImageRendererContext *context){[image drawInRect:CGRectMake(0,0,w,h)];}];
    still.width=w;still.height=h;still.rgba.resize(size_t(w)*h*4);
    CGColorSpaceRef space=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx=CGBitmapContextCreate(still.rgba.data(),w,h,8,w*4,space,kCGImageAlphaPremultipliedLast);
    CGContextDrawImage(ctx,CGRectMake(0,0,w,h),normalized.CGImage);CGContextRelease(ctx);CGColorSpaceRelease(space);
    // Shared blitter expects straight alpha; CoreGraphics supplies premultiplied.
    for(size_t i=0;i<still.rgba.size();i+=4) { const int a=still.rgba[i+3];if(a>0&&a<255)for(int c=0;c<3;++c)still.rgba[i+c]=uint8_t(std::min(255,int(still.rgba[i+c])*255/a)); }
}

}
@implementation AVSyncGenerator {
    std::unique_ptr<avsync::PhoneGeneratorPlan> _plan;
    avsync::PictureInfo _info;
    avsync::SourceStill _sourceImage;
    avsync::SourceStill _appIcon;
}
- (instancetype)initWithTestID:(NSString *)testID rate:(NSInteger)rate epochTaiNs:(int64_t)epoch epochHost:(double)host {
    if ((self = [super init])) {
        _plan=std::make_unique<avsync::PhoneGeneratorPlan>(testID.UTF8String,int(rate),epoch,host);
        _info.title="AVsync";_info.source="iPhone";
        _info.format_line="720p60 / 1200 baud / "+std::to_string(rate)+" Hz";
        _info.notes="Estimated phone clock/output";_info.offset_line="Set AV offset 0 ms";
        _info.period_ms=1000.0/rate;_info.show_sync_indicator=false;
        NSURL *bundleURL=[[NSBundle mainBundle] URLForResource:@"AvsyncResources" withExtension:@"bundle"];
        NSBundle *resources=bundleURL ? [NSBundle bundleWithURL:bundleURL] : nil;
        NSURL *iconURL=[resources URLForResource:@"icon" withExtension:@"png"];
        UIImage *icon=iconURL ? [UIImage imageWithContentsOfFile:iconURL.path] : nil;
        if(icon) { make_still(icon,_appIcon);_info.app_icon=&_appIcon; }
    }
    return self;
}
- (void)setSourceName:(NSString *)name subtitle:(NSString *)subtitle image:(UIImage * _Nullable)image {
    _info.source=name.length?name.UTF8String:"iPhone";_info.subtitle=subtitle.UTF8String;
    _sourceImage={};_info.source_image=nullptr;
    if(!image.CGImage)return;
    make_still(image,_sourceImage);
    _info.source_image=&_sourceImage;
}
- (UIImage *)syncForeground {
    avsync::Yuv422p10 frame;frame.resize(1280,720);
    avsync::render_sync_foreground(frame,_plan->period_frames(),_info.period_ms,true);
    return image_from_frame(frame,true);
}
- (NSDictionary *)geometry {
    auto g=avsync::qr_flash_geometry(1280,720);
    return @{ @"width":@1280,@"height":@720,@"markerX":@(g.marker_x),@"markerY":@(g.marker_y),@"markerSide":@(g.marker_side),@"stripX":@(g.strip_x),@"stripY":@(g.strip_y),@"stripWidth":@0,@"stripHeight":@0 };
}
- (NSDictionary *)frameAt:(double)target {
    const auto frame=_plan->frame_at(target);const int period=_plan->period_frames();const uint64_t seq=1+frame/period;const int pos=int(frame%period);
    const uint32_t data=(avsync::binary_to_gray(uint32_t(seq&0xffffff))<<4)|(pos&15);
    const uint32_t bits=(data<<4)|((__builtin_popcount(data)&1)<<3)|5;
    const auto dot=avsync::sync_indicator(1280,720,pos,period,_info.period_ms,true);uint8_t color[4];rgb(dot.y10,dot.cb10,dot.cr10,color);
    return @{ @"sequence":@(seq),@"position":@(pos),@"white":@(avsync::flash_white(pos,1000.0/60)),@"strip":@(bits),@"syncX":@(dot.x),@"syncY":@(dot.y),@"syncSize":@(dot.side),@"syncLineWidth":@2,@"syncR":@(color[0]),@"syncG":@(color[1]),@"syncB":@(color[2]),@"syncPositionMs":@(avsync::sync_place(pos,period,_info.period_ms).position_ms) };
}
- (double)hostTimeForSequence:(uint64_t)seq { return _plan->host_seconds(seq); }
- (NSData *)audioForSequence:(uint64_t)seq { const auto samples=_plan->audio(seq);return [NSData dataWithBytes:samples.data() length:samples.size()*sizeof(float)]; }
- (UIImage *)imageForSequence:(uint64_t)seq {
    _info.qr_json=avsync::qr_payload_text(_plan->qr(seq));_info.counter="Event "+std::to_string(seq);_info.time_line="Scheduled source output";
    avsync::Yuv422p10 frame;frame.resize(1280,720);
    avsync::render_picture(frame,_info,seq,0,_plan->period_frames(),false);
    return image_from_frame(frame);
}
@end
