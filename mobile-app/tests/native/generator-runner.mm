#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>
#import "AVSyncGenerator.h"
#import "AVSyncQRCodeReader.h"
#include "avsync/avsync.hpp"
#include <cassert>
#include <iostream>

extern NSString *AVSyncTextFontName();
using namespace avsync;
int main(int argc, char **argv) { @autoreleasepool {
    constexpr int width=1280,height=720;
    assert([AVSyncTextFontName() isEqualToString:@"DejaVuSans"]);
    for(int rate:{1,2}) {
        AVSyncGenerator *source=[[AVSyncGenerator alloc] initWithTestID:@"phone-native-test" rate:rate epochTaiNs:1700000037000000000LL epochHost:100];
        std::vector<float> signal(48000*3);
        for(int event=1;event<=3;++event) {
            const double host=[source hostTimeForSequence:event];
            assert(std::abs(host-(100+double(event-1)/rate))<1e-9);
            NSData *audio=[source audioForSequence:event];
            memcpy(signal.data()+4800+(event-1)*48000/rate,audio.bytes,audio.length);
            UIImage *image=[source imageForSequence:event];
            assert(image.CGImage && CGImageGetWidth(image.CGImage)==width);
            CVPixelBufferRef pixel=nullptr;
            assert(CVPixelBufferCreate(kCFAllocatorDefault,width,height,kCVPixelFormatType_32BGRA,(__bridge CFDictionaryRef)@{(id)kCVPixelBufferIOSurfacePropertiesKey:@{}},&pixel)==kCVReturnSuccess);
            CVPixelBufferLockBaseAddress(pixel,0);
            CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();
            CGContextRef context=CGBitmapContextCreate(CVPixelBufferGetBaseAddress(pixel),width,height,8,CVPixelBufferGetBytesPerRow(pixel),space,CGBitmapInfo(kCGBitmapByteOrder32Little)|CGBitmapInfo(kCGImageAlphaPremultipliedFirst));
            CGContextDrawImage(context,CGRectMake(0,0,width,height),image.CGImage);
            std::vector<uint16_t> luma(width*height);
            auto update_luma=[&] {
                auto *p=(uint8_t*)CVPixelBufferGetBaseAddress(pixel);
                for(int y=0;y<height;++y)for(int x=0;x<width;++x) luma[y*width+x]=uint16_t(64+876*int(p[y*CVPixelBufferGetBytesPerRow(pixel)+4*x+1])/255);
            };
            update_luma();
            auto *rgbPixels=(uint8_t*)CVPixelBufferGetBaseAddress(pixel);
            int chromatic=0;
            for(int bar=0;bar<7;++bar) {
                auto *sample=rgbPixels+100*CVPixelBufferGetBytesPerRow(pixel)+(bar*1280/7+90)*4;
                if(std::abs(int(sample[2])-int(sample[1]))>40||std::abs(int(sample[1])-int(sample[0]))>40)++chromatic;
            }
            assert(chromatic>=5); // Old grayscale source fails this.

            auto base=read_marker(luma.data(),width,height,width);
            assert(base.strip_ok && base.seq_low24==event && base.geometry_ok);
            // Exercise the same packed bits used by the native timing layers,
            // including all source-frame positions and pulse falling edges.
            const auto geometry=[source geometry];
            auto n=[&](NSString *key){return [geometry[key] intValue];};
            for(int pos=0;pos<60/rate;++pos) {
                NSDictionary *frame=[source frameAt:host+double(pos)/60];
                assert([frame[@"sequence"] intValue]==event);
                assert([frame[@"position"] intValue]==pos);
                const auto point=sync_indicator(width,height,pos,60/rate,1000.0/rate);
                assert([frame[@"syncX"] intValue]==point.x && [frame[@"syncY"] intValue]==point.y);
                assert([frame[@"syncSize"] intValue]==point.side);
                if(pos==0) assert([frame[@"syncR"] intValue]==255 && [frame[@"syncG"] intValue]==255);
                if(pos==1) assert([frame[@"syncG"] intValue]>[frame[@"syncR"] intValue]);
                if(pos==60/rate-1) assert([frame[@"syncR"] intValue]>[frame[@"syncG"] intValue]);
                const uint32_t bits=[frame[@"strip"] unsignedIntValue];
                for(int cell=0;cell<32;++cell) for(int y=n(@"stripY");y<n(@"stripY")+n(@"stripHeight");++y)for(int dx=0;dx<n(@"stripWidth");++dx)
                    luma[y*width+n(@"stripX")+cell*n(@"stripWidth")+dx]=bits&(1u<<(31-cell))?kYWhite:kYBlack;
                const bool white=[frame[@"white"] boolValue];
                for(int y=n(@"markerY");y<n(@"markerY")+n(@"markerSide");++y)for(int x=n(@"markerX");x<n(@"markerX")+n(@"markerSide");++x)luma[y*width+x]=white?kYWhite:kYBlack;
                auto reading=read_marker(luma.data(),width,height,width);
                assert(reading.strip_ok && reading.seq_low24==event && reading.frame_pos_mod16==(pos&15));
                assert((reading.state==MarkerReading::State::White)==(pos<6));
            }
            CVPixelBufferUnlockBaseAddress(pixel,0);
            NSDictionary *qr=[AVSyncQRCodeReader readPixelBuffer:pixel];
            bool found=false;
            for(NSString *payload in qr[@"payloads"]) if(auto parsed=parse_qr_payload(payload.UTF8String)) {
                assert(parsed->estimated_source && parsed->format=="720p60" && parsed->rate==rate && parsed->k==event);
                assert(parsed->tk==1700000037000000000LL+(event-1)*1000000000LL/rate); found=true;
            }
            assert(found);
            CGContextRelease(context);CGColorSpaceRelease(space);CVPixelBufferRelease(pixel);
        }
        UIImage *plain=[source imageForSequence:1];
        UIGraphicsImageRendererFormat *format=[UIGraphicsImageRendererFormat defaultFormat];format.scale=1;
        UIImage *logo=[[[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake(64,32) format:format] imageWithActions:^(UIGraphicsImageRendererContext *context) {
            [UIColor.magentaColor setFill];UIRectFill(CGRectMake(0,0,32,32));
            [UIColor.cyanColor setFill];UIRectFill(CGRectMake(32,0,32,32));
        }];
        [source setSourceName:@"Studio Zürich" subtitle:@"Camera A · SRT path" image:logo];
        UIImage *styled=[source imageForSequence:1];
        auto imageBytes=[&](UIImage *image) {
            std::vector<uint8_t> bytes(width*height*4);
            CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();
            CGContextRef ctx=CGBitmapContextCreate(bytes.data(),width,height,8,width*4,space,kCGImageAlphaPremultipliedLast);
            CGContextDrawImage(ctx,CGRectMake(0,0,width,height),image.CGImage);CGContextRelease(ctx);CGColorSpaceRelease(space);return bytes;
        };
        if(argc>1) {
            for(int pos:{0,1,60/rate-1}) {
                NSDictionary *state=[source frameAt:100+double(pos)/60];
                auto number=[&](NSString *key){return [state[key] doubleValue];};
                const auto geo=frame_geometry(width,height);
                UIImage *snapshot=[[[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake(width,height) format:format] imageWithActions:^(UIGraphicsImageRendererContext *ctx) {
                    [styled drawAtPoint:CGPointZero];
                    [([state[@"white"] boolValue]?UIColor.whiteColor:UIColor.blackColor) setFill];
                    UIRectFill(CGRectMake(geo.marker_x,geo.marker_y,geo.marker_side,geo.marker_side));
                    const uint32_t bits=[state[@"strip"] unsignedIntValue];
                    for(int cell=0;cell<32;++cell) { [(bits&(1u<<(31-cell))?UIColor.whiteColor:UIColor.blackColor) setFill];UIRectFill(CGRectMake(geo.strip_x+cell*geo.strip_w,geo.strip_y,geo.strip_w,geo.strip_h)); }
                    [[UIColor colorWithRed:number(@"syncR")/255 green:number(@"syncG")/255 blue:number(@"syncB")/255 alpha:1] setFill];
                    UIRectFill(CGRectMake(number(@"syncX"),number(@"syncY"),number(@"syncSize"),number(@"syncSize")));
                }];
                NSString *file=[NSString stringWithFormat:@"%s/%dhz-%d.png",argv[1],rate,pos];
                assert([UIImagePNGRepresentation(snapshot) writeToFile:file atomically:YES]);
            }
        }
        const auto before=imageBytes(plain),after=imageBytes(styled);
        int changed=0;
        const auto g=frame_geometry(width,height);
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            const size_t at=(size_t(y)*width+x)*4;
            const bool differs=!std::equal(before.begin()+at,before.begin()+at+4,after.begin()+at);
            if(differs) { ++changed;assert(y>g.marker_y+g.marker_side && y<g.strip_y); }
        }
        assert(changed>500); // Labels/logo actually visible, never cover timing/QR.
        [source setSourceName:@"" subtitle:@"" image:nil];
        const auto packets=decode_timecodes(signal.data(),signal.size(),48000);
        assert(packets.size()==3);
        for(int i=0;i<3;++i) {
            assert(packets[i].estimated_source && packets[i].live);
            assert(std::abs(packets[i].time_ms-(100+1000.0*i/rate))<.1);
            assert(packets[i].video_send_ns==quantize_timecode_ns(1700000037000000000LL+i*1000000000LL/rate));
        }
    }
    std::cout<<"PASS: shared color renderer, DejaVu text, custom name/subtitle/image, native sync scale, QR/strip/flash and 1/2-Hz independent audio.\n";
} }
