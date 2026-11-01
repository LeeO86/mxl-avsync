#include "avsync/avsync.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace avsync;
int main(){
 PlanConfig config;config.video=*parse_video_format("1080p50");config.event_rate=2;config.duration_ms=7000;config.timecode_baud=1200;config.source_id=timecode_source_id("deferred-test");
 auto plan=make_file_plan(config);constexpr int64_t epoch=1700000037000000000LL;
 for(auto&e:plan.events)e.audio_packet=encode_timecode(config.source_id,epoch+int64_t(std::llround(e.video_timeline_ms*1e6)),0,true);
 std::vector<float> audio(48000*7);AudioTimecodeWriter writer;writer.write(plan.events,0,audio.data(),audio.size(),1200);
 PhoneSession synchronous(2,{}),capture(2,{});std::optional<PhoneSession> frozen;size_t frozenSamples=0;
 for(size_t at=0;at<audio.size();at+=480){
  synchronous.push_audio(audio.data()+at,480,at/48.0,48000);
  capture.push_audio(audio.data()+at,480,at/48.0,48000,true);
  if((at+480)%48000==0){auto snapshot=capture;auto result=snapshot.finish();capture.merge_audio_analysis(snapshot);if(!frozen){frozen=snapshot;frozenSamples=result.audio_samples;}}
 }
 const auto a=synchronous.finish(),b=capture.finish();assert(a.audio_timecodes.size()==b.audio_timecodes.size()&&b.audio_timecodes.size()>=10);
 for(size_t i=0;i<a.audio_timecodes.size();++i){assert(a.audio_timecodes[i].audio_send_ns==b.audio_timecodes[i].audio_send_ns);assert(std::abs(a.audio_timecodes[i].time_ms-b.audio_timecodes[i].time_ms)<.1);}
 capture=PhoneSession(2,{});assert(frozen->finish().audio_samples==frozenSamples);
 std::cout<<"PASS: deferred audio decode + merged packet cache match synchronous onsets/history; snapshots survive reset\n";
}
