#import <Foundation/Foundation.h>
#import <CoreText/CoreText.h>
#include "avsync/avsync.hpp"
#include <algorithm>
#include <cmath>

namespace {
CGFontRef dejavu() {
    static CGFontRef face=nullptr;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        for (NSBundle *container in @[[NSBundle mainBundle], [NSBundle bundleForClass:NSClassFromString(@"AVSyncGenerator")]]) {
            NSURL *bundleURL=[container URLForResource:@"AvsyncResources" withExtension:@"bundle"];
            NSBundle *resources=bundleURL ? [NSBundle bundleWithURL:bundleURL] : nil;
            NSURL *url=[resources URLForResource:@"DejaVuSans" withExtension:@"ttf"];
            if (!url) continue;
            CGDataProviderRef provider=CGDataProviderCreateWithURL((__bridge CFURLRef)url);
            if(provider) { face=CGFontCreateWithDataProvider(provider);CGDataProviderRelease(provider); }
            if(face) break;
        }
    });
    return face;
}
CTFontRef font(int scale) {
    const CGFloat pixels=8*std::max(1,scale);
    return dejavu() ? CTFontCreateWithGraphicsFont(dejavu(),pixels,nullptr,nullptr) : CTFontCreateWithName(CFSTR("Helvetica"),pixels,nullptr);
}
CTLineRef line(std::string_view text,CTFontRef face) {
    NSString *string=[[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
    NSAttributedString *attributed=[[NSAttributedString alloc] initWithString:string ?: @"" attributes:@{(__bridge NSString*)kCTFontAttributeName:(__bridge id)face,(__bridge NSString*)kCTForegroundColorFromContextAttributeName:@YES}];
    return CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)attributed);
}
}
NSString *AVSyncTextFontName() {
    CTFontRef face=font(2); NSString *name=CFBridgingRelease(CTFontCopyPostScriptName(face));CFRelease(face);return name;
}
namespace avsync {
int text_px_width(std::string_view text,int scale) {
    if(text.empty())return 0;
    CTFontRef face=font(scale);CTLineRef textLine=line(text,face);
    const auto width=CTLineGetTypographicBounds(textLine,nullptr,nullptr,nullptr);
    CFRelease(textLine);CFRelease(face);return std::max(0,int(std::lround(width)));
}
int text_px_height(int scale) {
    CTFontRef face=font(scale);const auto height=CTFontGetAscent(face)+CTFontGetDescent(face);CFRelease(face);return std::max(1,int(std::ceil(height)));
}
void draw_text(LumaView view,int x,int y,std::string_view text,int scale,uint16_t value) {
    if(text.empty()||!view.data)return;
    const int w=std::max(1,text_px_width(text,scale)),h=text_px_height(scale);
    std::vector<uint8_t> alpha(size_t(w)*h,0);
    CGContextRef context=CGBitmapContextCreate(alpha.data(),w,h,8,w,nullptr,kCGImageAlphaOnly);
    if(!context)return;
    CTFontRef face=font(scale);CTLineRef textLine=line(text,face);
    CGContextSetGrayFillColor(context,1,1);
    CGContextSetTextPosition(context,0,h-CTFontGetAscent(face));
    CTLineDraw(textLine,context);
    for(int row=0;row<h;++row)for(int col=0;col<w;++col) {
        if(y+row<0||y+row>=view.height||x+col<0||x+col>=view.width)continue;
        const unsigned coverage=alpha[size_t(row)*w+col];
        auto& pixel=view.data[size_t(y+row)*view.stride+x+col];
        pixel=uint16_t((pixel*(255-coverage)+value*coverage+127)/255);
    }
    CFRelease(textLine);CFRelease(face);CGContextRelease(context);
}
}
