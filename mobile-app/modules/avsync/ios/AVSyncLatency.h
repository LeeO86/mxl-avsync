#pragma once
#include "avsync/avsync.hpp"

namespace avsync_phone {
struct SourceTime {
 const char* state = "waiting_for_live_qr";
 std::optional<double> video_utc_ms, audio_utc_ms;
};
// Source timestamps are TAI. This estimates a schedule; it is not clock lock.
inline SourceTime source_time(const avsync::QrPayload& qr,double observed_ms,double now_ms,
 const std::optional<avsync::VideoObservation>& video,double tai_utc_seconds) {
 SourceTime out;
 if(qr.mode!="LIVE"){out.state="file_signal";return out;}
 if(qr.tk<=0||!std::isfinite(tai_utc_seconds)||tai_utc_seconds<0||tai_utc_seconds>100)return out;
 if(!std::isfinite(now_ms)||!std::isfinite(observed_ms)||now_ms-observed_ms>3000||now_ms<observed_ms-100){out.state="qr_expired";return out;}
 if(!video||now_ms-video->time_ms>1500||video->time_ms>now_ms+100){out.state="waiting_for_video_event";return out;}
 auto format=avsync::parse_video_format(qr.format);
 if(!format||(qr.rate!=1&&qr.rate!=2&&qr.rate!=4)){out.state="unsupported_signal";return out;}
 int32_t delta=static_cast<int32_t>((video->seq-(qr.k&0xFFFFFFu))&0xFFFFFFu);
 if(delta>=0x800000)delta-=0x1000000;
 double period=avsync::plan_period(*format,qr.rate).period_ms;
 if(std::abs(delta)>8||std::abs(delta*period)>3000){out.state="qr_event_mismatch";return out;}
 out.video_utc_ms=static_cast<double>(qr.tk)/1e6+delta*period-tai_utc_seconds*1000;
 if(!qr.sweep&&std::isfinite(qr.offset_ms))out.audio_utc_ms=*out.video_utc_ms+qr.offset_ms;
 out.state="source_time_available";return out;
}
}
