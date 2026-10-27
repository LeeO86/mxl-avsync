#include "avsync/avsync.hpp"
#include <fstream>
#include <cstdlib>
#include <vector>
int main(int argc, char **argv) {
 std::string dir=argv[1];
 double offset=argc>2?std::atof(argv[2]):35; bool alternating=argc>3;
 using namespace avsync;
 constexpr int w=640,h=360,n=150;
 std::ofstream video(dir+"/video.gray",std::ios::binary);
 std::vector<PlannedEvent> events;
 for(int f=0;f<n;++f) {
  unsigned seq=1+f/25;
  std::vector<uint16_t> luma(w*h);
  LumaView view{luma.data(),w,h,w};
  fill_luma(view,kYBackground);draw_static_layer(view);draw_timing(view,seq,f%25,25,seq%2);
  std::vector<unsigned char> pixels(w*h);
  for(int i=0;i<w*h;++i)pixels[i]=luma[i]/4;
  video.write((char*)pixels.data(),pixels.size());
  if(f%25==0) { PlannedEvent e;e.chirp_id=seq%4;e.audio_centre_sample=(f*20.0+offset+(alternating && seq%2==0 ? 200.0 : 0.0))*48;events.push_back(e); }
 }
 std::vector<float> samples(n*20*48);
 synthesise_block(ChirpSpec{},events,0,samples.data(),samples.size());
 std::ofstream audio(dir+"/audio.f32",std::ios::binary);
 audio.write((char*)samples.data(),samples.size()*sizeof(float));
}
