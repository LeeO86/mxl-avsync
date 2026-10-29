#include "avsync/avsync.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
using namespace avsync;
int main() {
 constexpr int w=640,h=360,frames=960,rate=240;
 PhoneSession clean(2,{}), noisy(2,{}), cursor(2,{});
 for(int frame=0;frame<frames;frame++){
  const double time=frame*1000.0/rate;
  uint64_t seq=1+frame/120;
  std::vector<uint16_t> pixels(w*h);
  LumaView view{pixels.data(),w,h,w};fill_luma(view,kYBackground);draw_static_layer(view);
  int position=frame%120*25/120;
  draw_timing(view,seq,position,25,seq%2);
  clean.push_video(pixels.data(),w,h,w,time,0);
  // Parity-valid false codes persist across three frames, as do flicker edges.
  if(frame%120>=20&&frame%120<=22)draw_timing(view,seq,position,25,!(seq%2));
  if(frame%120>=40&&frame%120<=42)draw_timing(view,seq+1024,position,25,seq%2);
  noisy.push_video(pixels.data(),w,h,w,time,0);
  for(int y=h/2-3;y<h/2+3;++y)for(int x=w/2-3;x<w/2+3;++x)pixels[y*w+x]=seq%2?kYBlack:kYWhite;
  cursor.push_video(pixels.data(),w,h,w,time,0);
 }
 std::vector<PlannedEvent> events;
 for(int i=0;i<8;i++){PlannedEvent e;e.chirp_id=(i+1)%4;e.audio_centre_sample=(i*500+35)*48;events.push_back(e);}
 std::vector<float> audio(48000*4);synthesise_block(ChirpSpec{},events,0,audio.data(),audio.size());
 clean.push_audio(audio.data(),audio.size(),0,48000);noisy.push_audio(audio.data(),audio.size(),0,48000);cursor.push_audio(audio.data(),audio.size(),0,48000);
 auto a=clean.finish(),b=noisy.finish(),c=cursor.finish();int good=0;
 int cursorGood=0;for(const auto&e:c.events)if(e.av_offset_ms){assert(std::abs(*e.av_offset_ms-35)<2);++cursorGood;}
 assert(cursorGood>=6&&c.events.size()==a.events.size());
 for(const auto&e:b.events){if(e.av_offset_ms){assert(std::abs(*e.av_offset_ms-35)<2);++good;}}
 assert(good>=6);assert(a.events.size()==b.events.size());
 noisy.trim_before(2000);auto recent=noisy.finish();
 assert(!recent.events.empty());for(const auto&e:recent.events){assert(e.video_time_ms>=2000);if(e.av_offset_ms)assert(std::abs(*e.av_offset_ms-35)<2);}
 std::cout<<"PASS: "<<good<<" stable events at 240 fps; three-frame flicker and parity-valid sequence jumps rejected; first-frame timing and rolling trim preserved, including a sparse opposite-phase cursor.\n";
}
