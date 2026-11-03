// End to end through the real iOS bridge: the phone generator's AVS3 picture (720p60)
// is integrated by a simulated 239.48 fps camera into NV12 sample buffers (video 30 ms
// late), its timecode sound reaches the microphone 50 ms late, Vision reads the QR, and
// AVSyncPhoneAnalyzer measures. Prints the last report (or the calibration take) as JSON.
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import "AVSyncPhoneAnalyzer.h"
#import "AVSyncQRCodeReader.h"
#include "avsync/phone_generator.hpp"
#include "../../../generator/picture.hpp"
#include <cmath>
#include <iostream>

using namespace avsync;

static CMSampleBufferRef videoSample(const std::vector<uint8_t>& luma, int w, int h, double ms) {
    CVPixelBufferRef pixel = nullptr;
    CVPixelBufferCreate(kCFAllocatorDefault, w, h, kCVPixelFormatType_420YpCbCr8BiPlanarFullRange,
                        (__bridge CFDictionaryRef)@{(id)kCVPixelBufferIOSurfacePropertiesKey: @{}}, &pixel);
    CVPixelBufferLockBaseAddress(pixel, 0);
    auto *y = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pixel, 0);
    for (int row = 0; row < h; ++row) memcpy(y + row * CVPixelBufferGetBytesPerRowOfPlane(pixel, 0), luma.data() + size_t(row) * w, w);
    auto *uv = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pixel, 1);
    memset(uv, 128, CVPixelBufferGetBytesPerRowOfPlane(pixel, 1) * CVPixelBufferGetHeightOfPlane(pixel, 1));
    CVPixelBufferUnlockBaseAddress(pixel, 0);
    CMVideoFormatDescriptionRef format = nullptr;
    CMVideoFormatDescriptionCreateForImageBuffer(kCFAllocatorDefault, pixel, &format);
    CMSampleTimingInfo timing{kCMTimeInvalid, CMTimeMakeWithSeconds(ms / 1000, 1000000000), kCMTimeInvalid};
    CMSampleBufferRef sample = nullptr;
    CMSampleBufferCreateReadyWithImageBuffer(kCFAllocatorDefault, pixel, format, &timing, &sample);
    CFRelease(format);
    CVPixelBufferRelease(pixel);
    return sample;
}

static CMSampleBufferRef audioSample(const float *samples, size_t count, double ms) {
    AudioStreamBasicDescription asbd{48000, kAudioFormatLinearPCM, kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked, 4, 1, 4, 1, 32, 0};
    CMAudioFormatDescriptionRef format = nullptr;
    CMAudioFormatDescriptionCreate(kCFAllocatorDefault, &asbd, 0, nullptr, 0, nullptr, nullptr, &format);
    CMBlockBufferRef block = nullptr;
    CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, nullptr, count * 4, kCFAllocatorDefault, nullptr, 0, count * 4, 0, &block);
    CMBlockBufferReplaceDataBytes(samples, block, 0, count * 4);
    CMSampleBufferRef sample = nullptr;
    CMAudioSampleBufferCreateReadyWithPacketDescriptions(kCFAllocatorDefault, block, format, count, CMTimeMakeWithSeconds(ms / 1000, 1000000000), nullptr, &sample);
    CFRelease(block);
    CFRelease(format);
    return sample;
}

int main(int argc, char **argv) { @autoreleasepool {
    const bool calibration = argc > 1 && std::string(argv[1]) == "calibration";
    const int64_t epochTai = 1793300000000000000LL;
    const double epochHost = 100;
    PhoneGeneratorPlan plan("native-live", 2, epochTai, epochHost);
    const int w = 640, h = 360, frames = 60 * 7;
    // Each 60 Hz source frame, downscaled to the camera raster.
    std::vector<std::vector<uint8_t>> shown;
    Yuv422p10 picture; picture.resize(1280, 720);
    for (int n = 0; n < frames; ++n) {
        PictureInfo info; info.period_ms = 500; info.qr_json = qr_payload_text(plan.qr(plan.named_seq(n)));
        render_picture(picture, info, 1 + n / 30, n % 30, 30, plan.a_white(n));
        std::vector<uint8_t> small(size_t(w) * h);
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) small[size_t(y) * w + x] = uint8_t(picture.y[size_t(y * 2) * 1280 + x * 2] >> 2);
        shown.push_back(std::move(small));
    }
    const int rate = 48000;
    std::vector<float> mic(size_t(rate * 8), 0.f);
    for (uint64_t seq = 1; seq <= 14; ++seq) {
        const auto wave = plan.audio(seq, rate);
        const size_t at = size_t(std::lround((plan.host_seconds(seq) - epochHost + .050) * rate));
        for (size_t i = 0; i < wave.size() && at + i < mic.size(); ++i) mic[at + i] += wave[i];
    }
    if (calibration) for (int i = 0; i < 48; ++i) mic[size_t(2.3 * rate) + i] = i % 2 ? .9f : -.9f; // a clap at 2.3 s

    AVSyncPhoneAnalyzer *analyzer = [AVSyncPhoneAnalyzer new];
    [analyzer setDeviceUtcOffset:double(epochTai) / 1e6 - epochHost * 1000 - 37000];
    if (calibration) [analyzer beginCalibration];
    const double camera = 1000 / 239.48, start = epochHost * 1000;
    size_t audioAt = 0; int count = 0;
    NSDictionary *last = nil;
    std::vector<uint8_t> shot(size_t(w) * h);
    for (double t = start + 3.1; t < start + frames * 1000.0 / 60 - 40; t += camera) {
        const double s0 = t - start - 30, s1 = s0 + camera;
        if (s0 < 0) continue;
        std::vector<double> acc(shot.size(), 0);
        for (int n = int(std::floor(s0 * 60 / 1000)); n <= int(s1 * 60 / 1000) && n < frames; ++n) {
            const double o = std::min(s1, (n + 1) * 1000.0 / 60) - std::max(s0, n * 1000.0 / 60);
            if (o > 0) for (size_t i = 0; i < acc.size(); ++i) acc[i] += o * shown[n][i];
        }
        for (size_t i = 0; i < shot.size(); ++i) shot[i] = uint8_t(std::lround(acc[i] / camera));
        CMSampleBufferRef sample = videoSample(shot, w, h, t);
        [analyzer pushVideo:sample exposureMs:camera];
        if (count++ % 24 == 0 && [analyzer qrScanSuggestedAt:t])
            for (NSDictionary *detection in [AVSyncQRCodeReader readPixelBuffer:CMSampleBufferGetImageBuffer(sample)][@"detections"])
                [analyzer observeQRDetection:detection timeMs:t];
        CFRelease(sample);
        while (audioAt + 480 <= mic.size() && start + (audioAt + 480) * 1000.0 / rate <= t) {
            CMSampleBufferRef audio = audioSample(mic.data() + audioAt, 480, start + audioAt * 1000.0 / rate);
            [analyzer pushAudio:audio];
            CFRelease(audio);
            audioAt += 480;
        }
        if (calibration) {
            if (NSDictionary *take = [analyzer pollCalibration]) { last = take; break; }
        } else if ([analyzer reportDue]) {
            AVSyncPhoneAnalyzer *copy = [analyzer snapshotForPoll];
            if (NSDictionary *report = [copy poll]) last = report;
            [analyzer mergeAudio:copy];
        }
    }
    NSData *json = [NSJSONSerialization dataWithJSONObject:last ?: @{} options:0 error:nil];
    std::cout << std::string((const char *)json.bytes, json.length) << std::endl;
    return last ? 0 : 1;
}}
