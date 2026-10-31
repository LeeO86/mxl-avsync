#include "avsync/avsync.hpp"
#include "third_party/nayuki/qrcodegen.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
using namespace avsync;
int main() {
 constexpr int w=640,h=360;
 const auto g=qr_flash_geometry(w,h);const double unit=g.qr_size/41.0;
 for(int fps:{60,120,240})for(int rate:{1,2}) {
  QrFlashTracker tracker;std::vector<VideoObservation> events;
  std::vector<uint16_t> pixels(w*h,512);
  for(int i=0;i<fps*4;++i) {
   const double time=3+i*1000.0/fps,period=1000.0/rate;
   QrPayload p;p.timecode=p.qr_flash=true;p.source_id=5;p.test_id="cadence";
   p.rate=rate;p.k=1+int(time/period);p.tk=1793300000000000000LL+int64_t(p.k-1)*(1000000000/rate);p.format="1080p50";
   const auto symbol=qrcodegen::QrCode::encodeSegments(qrcodegen::QrSegment::makeSegments(qr_payload_text(p).c_str()),qrcodegen::QrCode::Ecc::QUARTILE,4,4,-1,false);
   for(int y=g.qr_y;y<g.qr_y+g.qr_size;++y)for(int x=g.qr_x;x<g.qr_x+g.qr_size;++x) {
    const int mx=int((x-g.qr_x)/unit)-4,my=int((y-g.qr_y)/unit)-4;
    pixels[y*w+x]=symbol.getModule(mx,my)?64:940;
   }
   for(int y=g.marker_y;y<g.marker_y+g.marker_side;++y)for(int x=g.marker_x;x<g.marker_x+g.marker_side;++x)pixels[y*w+x]=std::fmod(time,period)<40?940:64;
   if(i%(fps/10)==0) {
    QrDetection q;q.payload=p;
    q.x={(g.qr_x+4*unit)/w,(g.qr_x+37*unit)/w,(g.qr_x+37*unit)/w,(g.qr_x+4*unit)/w};
    q.y={(g.qr_y+4*unit)/h,(g.qr_y+4*unit)/h,(g.qr_y+37*unit)/h,(g.qr_y+37*unit)/h};
    tracker.observe(q,time);
   }
   VideoObservation frame;frame.time_ms=time;tracker.push(pixels.data(),w,h,w,frame);
   auto found=tracker.take_events();events.insert(events.end(),found.begin(),found.end());
  }
  assert(events.size()==size_t(4*rate-1));
  for(size_t i=0;i<events.size();++i) {
   const double onset=(i+1)*1000.0/rate;
   assert(events[i].seq==i+2 && events[i].timecode);
   assert(events[i].time_ms>=onset && events[i].time_ms-onset<=1000.0/fps+0.01);
  }
 }
 std::cout<<"PASS: AVS2 pulse/identity at 60/120/240 fps, 1/2 Hz, within one capture interval (synthetic pixels, known QR geometry)\n";
}
