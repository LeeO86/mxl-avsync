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
        QrFlashTracker tracker;std::vector<VideoObservation> detected;
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

            CVPixelBufferUnlockBaseAddress(pixel,0);
            NSDictionary *nativeQR=[AVSyncQRCodeReader readPixelBuffer:pixel];
            assert([nativeQR[@"detections"] count]==1);
            NSDictionary *detection=nativeQR[@"detections"][0];
            auto payload=parse_qr_payload([detection[@"payload"] UTF8String]);assert(payload&&payload->optical_code);
            QrDetection anchor;anchor.payload=*payload;NSArray *points=detection[@"points"];
            for(int i=0;i<4;++i){anchor.x[i]=[points[i][@"x"] doubleValue];anchor.y[i]=[points[i][@"y"] doubleValue];}
            CVPixelBufferLockBaseAddress(pixel,0);
            // Exercise the native timing layers at every source frame of the period: the
            // first burst only fills the field history, the next two are found.
            const auto geometry=[source geometry];
            auto n=[&](NSString *key){return [geometry[key] intValue];};
            for(int pos=0;pos<60/rate;++pos) {
                NSDictionary *frame=[source frameAt:host+double(pos)/60];
                assert([frame[@"sequence"] intValue]==event);
                assert([frame[@"position"] intValue]==pos);
                const auto point=sync_indicator(width,height,pos,60/rate,1000.0/rate,true);
                assert([frame[@"syncX"] intValue]==point.x && [frame[@"syncY"] intValue]==point.y);
                assert([frame[@"syncSize"] intValue]==point.side);
                if(pos==0) assert([frame[@"syncR"] intValue]==255 && [frame[@"syncG"] intValue]==255);
                if(pos==1) assert([frame[@"syncG"] intValue]>[frame[@"syncR"] intValue]);
                if(pos==60/rate-1) assert([frame[@"syncR"] intValue]>[frame[@"syncG"] intValue]);
                const uint32_t bits=[frame[@"strip"] unsignedIntValue];
                for(int cell=0;cell<32;++cell) for(int y=n(@"stripY");y<n(@"stripY")+n(@"stripHeight");++y)for(int dx=0;dx<n(@"stripWidth");++dx)
                    luma[y*width+n(@"stripX")+cell*n(@"stripWidth")+dx]=bits&(1u<<(31-cell))?kYWhite:kYBlack;
                // AVS3: field A (left half) white when "white", field B (right half) the opposite.
                const bool white=[frame[@"white"] boolValue];const int half=n(@"markerSide")/2;
                for(int y=n(@"markerY");y<n(@"markerY")+n(@"markerSide");++y)for(int x=n(@"markerX");x<n(@"markerX")+n(@"markerSide");++x)luma[y*width+x]=(x<n(@"markerX")+half)==white?kYWhite:kYBlack;
                const double time=1000.0*(event-1)/rate+1000.0*pos/60;
                if(pos%6==0&&(event>1||pos>=12))tracker.observe(anchor,time); // Repeated 100-ms scans; first identity arrives after the edge.
                VideoObservation capture;capture.time_ms=time;
                tracker.push(luma.data(),width,height,width,capture);
                auto events=tracker.take_events();detected.insert(detected.end(),events.begin(),events.end());
                if(event>1)assert(tracker.valid_frame());
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
        assert(detected.size()==2);
        for(int i=0;i<2;++i){assert(detected[i].seq==uint64_t(i+2));assert(std::abs(detected[i].time_ms-(i+1)*1000.0/rate)<1e-6);}
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
                const auto geo=qr_flash_geometry(width,height);
                UIImage *snapshot=[[[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake(width,height) format:format] imageWithActions:^(UIGraphicsImageRendererContext *ctx) {
                    [styled drawAtPoint:CGPointZero];
                    const bool white=[state[@"white"] boolValue];const int half=geo.marker_side/2;
                    [(white?UIColor.whiteColor:UIColor.blackColor) setFill];UIRectFill(CGRectMake(geo.marker_x,geo.marker_y,half,geo.marker_side));
                    [(white?UIColor.blackColor:UIColor.whiteColor) setFill];UIRectFill(CGRectMake(geo.marker_x+half,geo.marker_y,geo.marker_side-half,geo.marker_side));
                    [[UIColor colorWithRed:number(@"syncR")/255 green:number(@"syncG")/255 blue:number(@"syncB")/255 alpha:1] setFill];
                    UIRectFill(CGRectMake(number(@"syncX"),number(@"syncY"),number(@"syncSize"),number(@"syncSize")));
                    [UIColor.blackColor setFill];
                    UIRectFill(CGRectMake(number(@"syncX")+(number(@"syncSize")-number(@"syncLineWidth"))/2,number(@"syncY"),number(@"syncLineWidth"),number(@"syncSize")));
                    [[source syncForeground] drawAtPoint:CGPointZero];
                }];
                NSString *file=[NSString stringWithFormat:@"%s/%dhz-%d.png",argv[1],rate,pos];
                assert([UIImagePNGRepresentation(snapshot) writeToFile:file atomically:YES]);
            }
        }
        const auto before=imageBytes(plain),after=imageBytes(styled);
        int changed=0;
        const auto g=qr_flash_geometry(width,height);
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            const size_t at=(size_t(y)*width+x)*4;
            const bool differs=!std::equal(before.begin()+at,before.begin()+at+4,after.begin()+at);
            const bool inQr=x>=g.qr_x && x<g.qr_x+g.qr_size && y>=g.qr_y && y<g.qr_y+g.qr_size;
            if(differs && !inQr) { ++changed;assert(y>g.marker_y+g.marker_side && y<height); }
        }
        assert(changed>500); // Labels/logo actually visible, never cover timing. The QR carries the name (AVS4), so it may change.
        [source setSourceName:@"" subtitle:@"" image:nil];
        const auto packets=decode_timecodes(signal.data(),signal.size(),48000);
        assert(packets.size()==3);
        for(int i=0;i<3;++i) {
            assert(packets[i].estimated_source && packets[i].live);
            assert(std::abs(packets[i].time_ms-(100+1000.0*i/rate))<.1);
            assert(packets[i].video_send_ns==quantize_timecode_ns(1700000037000000000LL+i*1000000000LL/rate));
        }
    }
    // Sender calibration in the QR, and the calibration pattern helpers.
    {
        AVSyncGenerator *source=[[AVSyncGenerator alloc] initWithTestID:@"phone-native-test" rate:2 epochTaiNs:1700000037000000000LL epochHost:100];
        [source setSenderCalibrationMethod:@"transponder" offsetMs:-38.04 uncertaintyMs:1.0 day:2503];
        CVPixelBufferRef pixel=nullptr;
        assert(CVPixelBufferCreate(kCFAllocatorDefault,width,height,kCVPixelFormatType_32BGRA,(__bridge CFDictionaryRef)@{(id)kCVPixelBufferIOSurfacePropertiesKey:@{}},&pixel)==kCVReturnSuccess);
        CVPixelBufferLockBaseAddress(pixel,0);
        CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();
        CGContextRef context=CGBitmapContextCreate(CVPixelBufferGetBaseAddress(pixel),width,height,8,CVPixelBufferGetBytesPerRow(pixel),space,CGBitmapInfo(kCGBitmapByteOrder32Little)|CGBitmapInfo(kCGImageAlphaPremultipliedFirst));
        CGContextDrawImage(context,CGRectMake(0,0,width,height),[source imageForSequence:1].CGImage);
        CVPixelBufferUnlockBaseAddress(pixel,0);
        bool found=false;
        for(NSString *payload in [AVSyncQRCodeReader readPixelBuffer:pixel][@"payloads"]) if(auto parsed=parse_qr_payload(payload.UTF8String)) {
            assert(parsed->calibration.method=="transponder" && std::abs(parsed->calibration.offset_ms+38.0)<1e-9 && parsed->calibration.uncertainty_ms==1.0 && parsed->calibration.day==2503);found=true;
        }
        assert(found);
        CGContextRelease(context);CGColorSpaceRelease(space);CVPixelBufferRelease(pixel);
        [source setSenderCalibrationMethod:@"bogus" offsetMs:1 uncertaintyMs:1 day:1]; // ignored: method none
        assert([AVSyncGenerator calibrationLeftWhiteAtFrame:29 fps:60] && ![AVSyncGenerator calibrationLeftWhiteAtFrame:30 fps:60]);
        assert([AVSyncGenerator calibrationSwapFrame:2 fps:60]==60);
        NSData *beep=[AVSyncGenerator calibrationBeepWithSampleRate:48000];
        assert(beep.length==192*sizeof(float));
        assert(![[AVSyncGenerator displayCalibrationForSamples:[NSMutableData dataWithLength:48000*sizeof(float)] sampleRate:48000 minimumPairs:8][@"ok"] boolValue]);
    }
    std::cout<<"PASS: shared color renderer, DejaVu text, custom name/subtitle/image, native sync scale, QR geometry/flash onset and 1/2-Hz independent audio.\n";
} }
