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
 AudioTimecode packet;packet.live=true;packet.source_id=7;packet.time_ms=1155;
 packet.video_send_ns=1700000038000000000LL;packet.audio_send_ns=packet.video_send_ns+35000000;
 packet.bytes=encode_timecode(packet.source_id,packet.video_send_ns,35000000,true);
 Measurement event;event.have_video=event.have_audio=event.audio_timecode=true;
 event.source_id=7;event.audio_send_ns=packet.audio_send_ns;event.grade="pass";
 event.video_time_ms=1120;event.audio_time_ms=1155;event.av_offset_ms=35;event.expected_av_offset_ms=35;
 const double wall=1700000000000.0;
 auto paired=[&]{return paired_video_latency(packet,event,1400,wall,37);};
 assert(paired()&&std::abs(*paired()-120)<0.01);
 // Calibration/median must not enter the per-channel timestamp calculation.
 event.av_offset_ms=999;assert(paired()&&std::abs(*paired()-120)<0.01);
 event.source_id=8;assert(!paired());event.source_id=7;
 event.audio_send_ns++;assert(!paired());event.audio_send_ns--;
 event.audio_time_ms+=500;assert(!paired());event.audio_time_ms-=500;
 event.expected_av_offset_ms=0;assert(!paired());event.expected_av_offset_ms=35;
 event.grade="invalid";assert(!paired());event.grade="pass";
 event.have_video=false;assert(!paired());event.have_video=true;
 assert(!paired_video_latency(packet,event,4000,wall,37));
 assert(!paired_video_latency(packet,event,1000,wall,37));
 event.video_time_ms=-500;assert(!paired());event.video_time_ms=1120;
 packet.live=false;assert(!paired());packet.live=true;
 assert(!paired_video_latency(packet,event,1400,wall,NAN));
 std::cout<<"PASS: QR source time, TAI/UTC, wrap, configured source offset, sweep, expiry, unsupported rate and missing events.\n";
 std::cout<<"PASS: exact paired audio timestamp supplies video latency; wrong source/event, stale/future, file and invalid pairs rejected; calibration independent.\n";
}
