#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>
#import "AVSyncGenerator.h"
#import "AVSyncQRCodeReader.h"
#include "avsync/avsync.hpp"
#include <cassert>
#include <iostream>

using namespace avsync;
int main() { @autoreleasepool {
    constexpr int width=1280,height=720;
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
        const auto packets=decode_timecodes(signal.data(),signal.size(),48000);
        assert(packets.size()==3);
        for(int i=0;i<3;++i) {
            assert(packets[i].estimated_source && packets[i].live);
            assert(std::abs(packets[i].time_ms-(100+1000.0*i/rate))<.1);
            assert(packets[i].video_send_ns==quantize_timecode_ns(1700000037000000000LL+i*1000000000LL/rate));
        }
    }
    std::cout<<"PASS: actual iOS generator QR/geometry, every 60-Hz strip/flash phase, 1/2-Hz blind audio identity and estimated source flag.\n";
} }
