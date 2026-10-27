#include "AVSyncLatency.h"
#include <cassert>
#include <iostream>
using namespace avsync; using namespace avsync_phone;
int main(){
 QrPayload qr; qr.mode="LIVE";qr.k=0x1000001;qr.tk=1700000037000000000;qr.offset_ms=35;
 VideoObservation video;video.seq=2;video.time_ms=1000;
 auto result=source_time(qr,900,1100,video,37);
 assert(result.video_utc_ms&&std::abs(*result.video_utc_ms-1700000000500.0)<0.01);
 assert(result.audio_utc_ms&&std::abs(*result.audio_utc_ms-*result.video_utc_ms-35)<0.01);
 qr.sweep=true; assert(!source_time(qr,900,1100,video,37).audio_utc_ms);qr.sweep=false;
 qr.mode="FILE";assert(!source_time(qr,900,1100,video,37).video_utc_ms);qr.mode="LIVE";
 assert(!source_time(qr,0,4000,video,37).video_utc_ms);
 assert(!source_time(qr,900,1100,std::nullopt,37).video_utc_ms);
 qr.k=100;assert(!source_time(qr,900,1100,video,37).video_utc_ms);
 qr.k=0xFFFFFF;video.seq=0;assert(source_time(qr,900,1100,video,37).video_utc_ms);
 qr.rate=3;assert(!source_time(qr,900,1100,video,37).video_utc_ms);
 std::cout<<"PASS: QR source time, TAI/UTC, wrap, configured source offset, sweep, expiry, unsupported rate and missing events.\n";
}
