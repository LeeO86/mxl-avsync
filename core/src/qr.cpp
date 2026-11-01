#include "avsync/avsync.hpp"
#include "../../third_party/nayuki/qrcodegen.hpp"

#include <cctype>
#include <charconv>
#include <cstdlib>
#include <sstream>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace avsync {
namespace {

void skip_ws(std::string_view& in)
{
    while (!in.empty() && std::isspace(static_cast<unsigned char>(in.front()))) {
        in.remove_prefix(1);
    }
}

bool consume(std::string_view& in, char ch)
{
    skip_ws(in);
    if (in.empty() || in.front() != ch) {
        return false;
    }
    in.remove_prefix(1);
    return true;
}

std::optional<std::string> parse_string(std::string_view& in)
{
    skip_ws(in);
    if (in.empty() || in.front() != '"') {
        return std::nullopt;
    }
    in.remove_prefix(1);
    std::string out;
    while (!in.empty() && in.front() != '"') {
        if (in.front() == '\\' && in.size() >= 2) {
            out.push_back(in[1]);
            in.remove_prefix(2);
            continue;
        }
        out.push_back(in.front());
        in.remove_prefix(1);
    }
    if (in.empty() || in.front() != '"') {
        return std::nullopt;
    }
    in.remove_prefix(1);
    return out;
}

std::optional<double> parse_number(std::string_view& in)
{
    skip_ws(in);
    if (in.empty()) {
        return std::nullopt;
    }
    const char* begin = in.data();
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (end == begin) {
        return std::nullopt;
    }
    in.remove_prefix(static_cast<size_t>(end - begin));
    return value;
}

} // namespace

std::string qr_payload_json(const QrPayload& payload)
{
    std::ostringstream os;
    os.setf(std::ios::fixed);
    os.precision(0);
    os << "{\"s\":\"" << payload.schema << "\",\"t\":\"" << payload.test_id << "\",\"m\":\"" << payload.mode << "\",\"k\":" << payload.k
       << ",\"tk\":" << payload.tk << ",\"o\":";
    if (payload.sweep) {
        os << "\"sweep\"";
    } else {
        os << static_cast<long long>(std::llround(payload.offset_ms));
    }
    os << ",\"r\":" << payload.rate << ",\"f\":\"" << payload.format << "\"";
    if (payload.timecode) os << ",\"tc\":\"v3\",\"sid\":" << payload.source_id;
    if (payload.estimated_source) os << ",\"clk\":\"phone-utc\"";
    os << '}';
    return os.str();
}

namespace {
constexpr const char* formats[] = {"1080p50", "1080p25", "1080p59.94", "1080i25", "2160p50", "720p60", "720p50", "960x540p25", "540p25"};
constexpr char hex[] = "0123456789ABCDEF";
std::optional<QrPayload> parse_avs2(std::string_view text) {
    if (text.size() != 59 || text.substr(0,5) != "AVS2:") return std::nullopt;
    std::array<uint8_t,27> b{};
    for (size_t i=0;i<b.size();++i) {
        auto digit=[](char c)->int { if(c>='0'&&c<='9')return c-'0';if(c>='A'&&c<='F')return c-'A'+10;return -1; };
        int hi=digit(text[5+2*i]),lo=digit(text[6+2*i]);if(hi<0||lo<0)return std::nullopt;
        b[i]=uint8_t(hi*16+lo);
    }
    if((b[0]&~3)||b[25]>=std::size(formats)||(b[26]!=1&&b[26]!=2))return std::nullopt;
    auto read=[&](int start,int n){uint64_t v=0;for(int i=0;i<n;++i)v=(v<<8)|b[start+i];return v;};
    if(read(13,8)>uint64_t(INT64_MAX))return std::nullopt;
    QrPayload p;p.qr_flash=p.timecode=true;p.mode=(b[0]&1)?"LIVE":"FILE";p.estimated_source=b[0]&2;
    if(p.estimated_source&&p.mode!="LIVE")return std::nullopt;
    p.source_id=uint32_t(read(1,4));p.k=read(5,8);p.tk=int64_t(read(13,8));
    const uint32_t oq=uint32_t(read(21,4));
    p.offset_ms=(oq<=INT32_MAX?int64_t(oq):int64_t(oq)-0x100000000LL)/16.0;
    p.format=formats[b[25]];p.rate=b[26];p.test_id="source-"+std::string(text.substr(7,8));
    return p;
}
}
std::string qr_payload_text(const QrPayload& p) {
    if(!p.qr_flash)return qr_payload_json(p);
    auto format=std::find(std::begin(formats),std::end(formats),p.format);
    if(!p.timecode||format==std::end(formats)||(p.rate!=1&&p.rate!=2)||p.tk<0||
       (p.mode!="LIVE"&&p.mode!="FILE")||!std::isfinite(p.offset_ms)||std::abs(p.offset_ms)>134217727||
       (p.estimated_source&&p.mode!="LIVE"))throw std::invalid_argument("invalid AVS2 payload");
    std::array<uint8_t,27>b{};b[0]=(p.mode=="LIVE"?1:0)|(p.estimated_source?2:0);
    auto put=[&](int start,int n,uint64_t v){for(int i=n-1;i>=0;--i){b[start+i]=uint8_t(v);v>>=8;}};
    put(1,4,p.source_id);put(5,8,p.k);put(13,8,uint64_t(p.tk));put(21,4,uint32_t(int32_t(std::llround(p.offset_ms*16))));
    b[25]=uint8_t(format-std::begin(formats));b[26]=uint8_t(p.rate);
    std::string out="AVS2:";for(uint8_t byte:b){out+=hex[byte>>4];out+=hex[byte&15];}return out;
}

std::optional<QrPayload> parse_qr_payload(std::string_view json)
{
    if(json.starts_with("AVS2:"))return parse_avs2(json);
    QrPayload payload;
    std::string_view in = json;
    if (!consume(in, '{')) {
        return std::nullopt;
    }
    bool any = false;
    while (true) {
        skip_ws(in);
        if (!in.empty() && in.front() == '}') {
            in.remove_prefix(1);
            break;
        }
        if (any && !consume(in, ',')) {
            return std::nullopt;
        }
        any = true;
        const auto key = parse_string(in);
        if (!key || !consume(in, ':')) {
            return std::nullopt;
        }
        skip_ws(in);
        if (!in.empty() && in.front() == '"') {
            const auto text = parse_string(in);
            if (!text) {
                return std::nullopt;
            }
            if (*key == "clk") { if (*text != "phone-utc") return std::nullopt; payload.estimated_source = true; }
            else if (*key == "tc") { payload.timecode = *text == "v3"; }
            else if (*key == "s") {
                payload.schema = *text;
            } else if (*key == "t") {
                payload.test_id = *text;
            } else if (*key == "m") {
                payload.mode = *text;
            } else if (*key == "o") {
                payload.sweep = (*text == "sweep");
            } else if (*key == "f") {
                payload.format = *text;
            }
        } else {
            if (*key == "tk" || *key == "k" || *key == "sid") {
                skip_ws(in);
                int64_t integer = 0;
                const auto result = std::from_chars(in.data(), in.data() + in.size(), integer);
                if (result.ec != std::errc{} || integer < 0) return std::nullopt;
                in.remove_prefix(size_t(result.ptr - in.data()));
                if (*key == "tk") payload.tk = integer;
                else if (*key == "k") payload.k = uint64_t(integer);
                else { if (uint64_t(integer) > UINT32_MAX) return std::nullopt; payload.source_id = uint32_t(integer); }
                continue;
            }
            const auto number = parse_number(in);
            if (!number) {
                return std::nullopt;
            }
            if (*key == "sid") { payload.source_id = static_cast<uint32_t>(*number); }
            else if (*key == "k") {
                payload.k = static_cast<uint64_t>(*number);
            } else if (*key == "tk") {
                payload.tk = static_cast<int64_t>(*number);
            } else if (*key == "o") {
                payload.offset_ms = *number;
                payload.sweep = false;
            } else if (*key == "r") {
                payload.rate = static_cast<int>(*number);
            }
        }
    }
    if (payload.schema != kQrSchema || payload.test_id.empty() || (payload.mode != "FILE" && payload.mode != "LIVE") || (payload.rate != 1 && payload.rate != 2 && payload.rate != 4) || (payload.timecode && payload.rate == 4)) {
        return std::nullopt;
    }
    return payload;
}

bool flash_white(int frame, double grain_ms) {
    return frame >= 0 && frame * grain_ms < 100;
}

bool identify_video(VideoObservation& video, const QrPayload& qr) {
    if (!qr.timecode || (qr.rate != 1 && qr.rate != 2)) return false;
    if(qr.qr_flash) {
        if(video.seq!=qr.k)return false;
        video.timecode=true;video.source_id=qr.source_id;video.estimated_source=qr.estimated_source;
        video.event_time_ns=quantize_timecode_ns(qr.tk);
        if(qr.mode=="LIVE"){video.have_set_time=true;video.set_tai_ns=qr.tk;}return true;
    }
    const auto format = parse_video_format(qr.format);
    if (!format) return false;
    int32_t delta = int32_t((video.seq - (qr.k & 0xFFFFFFu)) & 0xFFFFFFu);
    if (delta >= 0x800000) delta -= 0x1000000;
    if (std::abs(delta) > 60) return false;
    const auto period = plan_period(*format, qr.rate);
    const int64_t period_ns = index_to_ns(format->grain_rate(), period.grains);
    const int64_t send = qr.tk + int64_t(delta) * period_ns;
    if (send < 0) return false;
    video.estimated_source = qr.estimated_source;
    video.timecode = true; video.source_id = qr.source_id;
    video.event_time_ns = quantize_timecode_ns(send);
    if (qr.mode == "LIVE") { video.have_set_time = true; video.set_tai_ns = send; }
    return true;
}


FrameGeometry qr_flash_geometry(int width, int height) {
    auto g=frame_geometry(width,height);
    const int unit=std::max(1,int(height*0.40)/41);
    g.qr_size=g.marker_side=41*unit;
    g.qr_x=(width-86*unit)/2;g.qr_y=std::max(0,int(height*.23));
    g.marker_x=g.qr_x+45*unit;g.marker_y=g.qr_y;
    g.sweep_x0=width/5;g.sweep_x1=width*4/5;g.sweep_y=int(height*.77);
    return g;
}

namespace {
bool qr_transform(const QrDetection& q, std::array<double,9>& H) {
    double a[8][9]{};
    for(int i=0;i<4;++i) {
        const double x=(i==1||i==2)?1:0,y=i>=2?1:0,u=q.x[i],v=q.y[i];
        if(!std::isfinite(u)||!std::isfinite(v)||u<0||u>1||v<0||v>1)return false;
        a[2*i][0]=x;a[2*i][1]=y;a[2*i][2]=1;a[2*i][6]=-u*x;a[2*i][7]=-u*y;a[2*i][8]=u;
        a[2*i+1][3]=x;a[2*i+1][4]=y;a[2*i+1][5]=1;a[2*i+1][6]=-v*x;a[2*i+1][7]=-v*y;a[2*i+1][8]=v;
    }
    // Require a convex, non-degenerate, consistently ordered quadrilateral.
    double sign=0;
    for(int i=0;i<4;++i){int j=(i+1)%4,k=(i+2)%4;
        double cross=(q.x[j]-q.x[i])*(q.y[k]-q.y[j])-(q.y[j]-q.y[i])*(q.x[k]-q.x[j]);
        if(std::abs(cross)<1e-7||(i&&cross*sign<=0))return false;sign=cross;
    }
    for(int col=0;col<8;++col) {
        int pivot=col;for(int row=col+1;row<8;++row)if(std::abs(a[row][col])>std::abs(a[pivot][col]))pivot=row;
        if(std::abs(a[pivot][col])<1e-12)return false;
        for(int j=col;j<9;++j)std::swap(a[col][j],a[pivot][j]);
        double d=a[col][col];for(int j=col;j<9;++j)a[col][j]/=d;
        for(int row=0;row<8;++row)if(row!=col){double m=a[row][col];for(int j=col;j<9;++j)a[row][j]-=m*a[col][j];}
    }
    for(int i=0;i<8;++i)H[i]=a[i][8];H[8]=1;return true;
}
bool project_qr(const std::array<double,9>& H,double mx,double my,double& x,double& y) {
    const double u=mx/33,v=my/33,d=H[6]*u+H[7]*v+1;
    if(!std::isfinite(d)||d<=1e-8)return false;
    x=(H[0]*u+H[1]*v+H[2])/d;y=(H[3]*u+H[4]*v+H[5])/d;
    return std::isfinite(x)&&std::isfinite(y);
}
bool same_qr_source(const QrPayload&a,const QrPayload&b) {
    return a.source_id==b.source_id&&a.mode==b.mode&&a.rate==b.rate&&a.format==b.format&&a.estimated_source==b.estimated_source;
}
}
void QrFlashTracker::reset() { *this=QrFlashTracker{}; }
bool QrFlashTracker::qr_scan_suggested(double time) const {
    if(!anchor_||!pose_.found||time-anchor_ms_>=350||time<anchor_ms_)return true;
    if(edges_.empty())return true;
    const auto format=parse_video_format(anchor_->payload.format);if(!format)return true;
    const double period=plan_period(*format,anchor_->payload.rate).period_ms;
    const double since=time-edges_.back().frame.time_ms;
    // A QR from the first 25 ms cannot identify this pulse. Photometry still
    // samples every frame; this gate only schedules the independent QR worker.
    if(since>=0&&since<25)return false;
    if(!edges_.back().emitted&&since>=25&&since<=period*.85)return true;
    const double phase=std::fmod(std::max(0.0,since),period);
    return phase<=160||phase>=period-80;
}
void QrFlashTracker::observe(const QrDetection& q,double time) {
    if(!q.payload.qr_flash||!q.payload.timecode||!std::isfinite(time)||!parse_video_format(q.payload.format))return;
    std::array<double,9> h{};if(!qr_transform(q,h))return;
    if(anchor_&&time<anchor_ms_)return; // Asynchronous results retain capture order.
    if(anchor_&&!same_qr_source(q.payload,anchor_->payload))reset();
    if(anchor_){
        double movement=0;for(int i=0;i<4;++i)movement=std::max(movement,std::hypot(q.x[i]-anchor_->x[i],q.y[i]-anchor_->y[i]));
        const double side=std::hypot(q.x[1]-q.x[0],q.y[1]-q.y[0]);
        if(movement>std::max(.005,side*.08)){phase_=MarkerReading::State::Invalid;blacks_=0;edges_.clear();codes_.clear();}
    }
    anchor_=q;anchor_ms_=time;transform_=h;
    std::array<uint8_t,1089> modules{};
    if(!codes_.empty()&&qr_payload_text(codes_.back().payload)==qr_payload_text(q.payload))modules=codes_.back().modules;
    else {
        const auto text=qr_payload_text(q.payload);
        const auto symbol=qrcodegen::QrCode::encodeSegments(qrcodegen::QrSegment::makeSegments(text.c_str()),qrcodegen::QrCode::Ecc::QUARTILE,4,4,-1,false);
        for(int y=0;y<33;++y)for(int x=0;x<33;++x)modules[y*33+x]=symbol.getModule(x,y)?0:1;
    }
    codes_.push_back({q.payload,time,modules});while(codes_.size()>32||(!codes_.empty()&&time-codes_.front().time>2000))codes_.pop_front();
    match();
}
void QrFlashTracker::push(const uint16_t* pixels,int width,int height,int stride,VideoObservation frame) { push_pixels(pixels,false,width,height,stride,frame); }
void QrFlashTracker::push_u8(const uint8_t* pixels,int width,int height,int stride,VideoObservation frame) { push_pixels(pixels,true,width,height,stride,frame); }
void QrFlashTracker::push_pixels(const void* pixels,bool eight_bit,int width,int height,int stride,VideoObservation frame) {
    ++diagnostics_.frames;
    valid_=false;pose_.found=false;
    auto lose=[&]{phase_=MarkerReading::State::Invalid;blacks_=0;};
    if(!anchor_||!pixels||width<16||height<16||stride<width||!std::isfinite(frame.time_ms)) {lose();return;}
    const double gap=frame.time_ms-last_frame_ms_;last_frame_ms_=frame.time_ms;
    if(gap<=0||gap>60)lose();
    if(frame.time_ms-anchor_ms_>750||frame.time_ms-anchor_ms_<-50){++diagnostics_.geometry_rejections;lose();return;}
    ++diagnostics_.probe_frames;
    auto sample=[&](double mx,double my)->double{
        double x,y;if(!project_qr(transform_,mx,my,x,y)||x<0||y<0||x>1||y>1)return -1;
        int ix=int(std::lround(x*width)),iy=int(std::lround(y*height));
        if(ix<0||iy<0||ix>=width||iy>=height)return -1;
        return eight_bit ? static_cast<const uint8_t*>(pixels)[size_t(iy)*stride+ix]*4 : static_cast<const uint16_t*>(pixels)[size_t(iy)*stride+ix];
    };
    auto patch=[&](double x,double y,double radius)->double {
        std::array<double,9> p{};int n=0;
        for(int j=-1;j<=1;++j)for(int i=-1;i<=1;++i){double v=sample(x+i*radius,y+j*radius);if(v<0)return -1;p[n++]=v;}
        std::sort(p.begin(),p.end());return p[4];
    };
    double black=0,white=0;int refs=0;
    for(auto center: {std::pair{3.5,3.5},std::pair{29.5,3.5},std::pair{3.5,29.5}}) {
        double b=patch(center.first,center.second,.35),w=patch(center.first-2,center.second,.12);
        if(b>=0&&w-b>=100){black+=b;white+=w;++refs;}
    }
    if(refs<2){++diagnostics_.finder_rejections;lose();return;}
    black/=refs;white/=refs;const double contrast=white-black;
    std::array<double,25> p{};int n=0;
    // Flash panel spans [41,82] x [-4,37] in QR-symbol module coordinates.
    for(int j=0;j<5;++j)for(int i=0;i<5;++i) {
        double v=sample(41+41*(.30+.10*i),-4+41*(.30+.10*j));if(v<0){lose();return;}p[n++]=v;
    }
    std::sort(p.begin(),p.end());double light=p[12];
    auto trace=[&](int phase){
        if(phase!=0||diagnostics_.samples.empty()||frame.time_ms-diagnostics_.samples.back().time_ms>=100){
            diagnostics_.samples.push_back({frame.time_ms,(light-black)/contrast,(p[23]-p[1])/contrast,phase});
            while(diagnostics_.samples.size()>64)diagnostics_.samples.pop_front();
        }
    };
    auto mixed=[&]{if(phase_!=MarkerReading::State::Black||frame.time_ms-last_black_ms_>60)lose();};
    if(p[23]-p[1]>contrast*.40){++diagnostics_.mixed_frames;trace(1);mixed();return;}
    auto phase=light<black+.30*contrast?MarkerReading::State::Black:
        light>black+.70*contrast?MarkerReading::State::White:MarkerReading::State::Invalid;
    if(phase==MarkerReading::State::Invalid){++diagnostics_.mixed_frames;trace(1);mixed();return;}
    trace(phase==MarkerReading::State::White?2:0);
    valid_=true;
    const double xs[]={-4,82,82,-4},ys[]={-4,-4,37,37};
    for(int i=0;i<4;++i){double x,y;if(!project_qr(transform_,xs[i],ys[i],x,y)){lose();valid_=false;return;}pose_.corner_x[i]=x*width;pose_.corner_y[i]=y*height;}
    pose_.found=true;pose_.origin_x=pose_.corner_x[0];pose_.origin_y=pose_.corner_y[0];
    pose_.width=std::hypot(pose_.corner_x[1]-pose_.corner_x[0],pose_.corner_y[1]-pose_.corner_y[0]);
    pose_.height=std::hypot(pose_.corner_x[3]-pose_.corner_x[0],pose_.corner_y[3]-pose_.corner_y[0]);
    if(phase==MarkerReading::State::White) {
        if(phase_==MarkerReading::State::Black&&blacks_>=2) {
            frame.event=true;frame.marker_white=true;frame.quality="good";frame.contrast=contrast;frame.flags.push_back("qr_flash");frame.flags.push_back("frame_counter_unavailable");
            const auto g=qr_flash_geometry(width,height);const double unit=g.qr_size/41.0;
            const double ex[]={g.qr_x+4*unit,g.qr_x+37*unit,g.qr_x+37*unit,g.qr_x+4*unit};
            const double ey[]={g.qr_y+4*unit,g.qr_y+4*unit,g.qr_y+37*unit,g.qr_y+37*unit};
            bool optical=anchor_->payload.estimated_source;
            for(int i=0;i<4;++i)optical|=std::hypot(anchor_->x[i]*width-ex[i],anchor_->y[i]*height-ey[i])>3;
            if(optical)frame.flags.push_back("optical_capture");
            edges_.push_back({frame,1,false,{}});
            ++diagnostics_.edges;
        } else if(phase_==phase&&!edges_.empty()&&frame.time_ms-edges_.back().frame.time_ms<=150)++edges_.back().whites;
        if(!edges_.empty()&&edges_.back().symbols.size()<32&&!edges_.back().emitted&&frame.time_ms-edges_.back().frame.time_ms<=100) {
            // Keep sparse module-center evidence from the pulse itself. A later
            // decoded identity must match this image, not just a nearby time.
            std::array<uint8_t,1089> symbol{};
            ++diagnostics_.symbol_frames;
            diagnostics_.module_reads+=symbol.size();
            for(int y=0;y<33;++y)for(int x=0;x<33;++x) {
                const double value=sample(x+.5,y+.5);
                symbol[y*33+x]=value<0?2:value<black+.35*contrast?0:value>black+.65*contrast?1:2;
            }
            edges_.back().symbols.push_back(symbol);
            match(); // New pulse evidence can change the identity decision.
        }
        blacks_=0;
    }else {++blacks_;last_black_ms_=frame.time_ms;}
    phase_=phase;
    while(!edges_.empty()&&frame.time_ms-edges_.front().frame.time_ms>2000)edges_.pop_front();
    // Black frames do not change symbols or decoded QR evidence. QR arrivals
    // call match() in observe(); retrying here would repeat identical work.
}
void QrFlashTracker::match() {
    ++diagnostics_.match_calls;
    for(auto& edge:edges_) {
        if(edge.emitted||edge.whites<2)continue;
        for(const auto& code:codes_) {
            const auto format=parse_video_format(code.payload.format);if(!format)continue;
            const double period=plan_period(*format,code.payload.rate).period_ms;
            const double after=code.time-edge.frame.time_ms;
            // Decode away from a scan-out boundary; never infer missing identities.
            if(after<25||after>period*.85)continue;
            // A code already shown well before this pulse is an old/frozen QR.
            if(std::any_of(codes_.begin(),codes_.end(),[&](const Code& old){return old.payload.k==code.payload.k&&old.time<edge.frame.time_ms-25;}))continue;
            bool symbol_matches=false;
            for(const auto& symbol:edge.symbols) {
                int known=0,wrong=0;
                for(size_t i=0;i<symbol.size();++i)if(symbol[i]<2){
                    ++known;++diagnostics_.symbol_comparisons;
                    if(symbol[i]!=code.modules[i]){wrong=1;break;}
                }
                if(known>=int(symbol.size()*.80)&&wrong==0){symbol_matches=true;break;}
            }
            if(!symbol_matches)continue;
            if(emitted_seq_&&code.payload.k<=*emitted_seq_)continue;
            auto out=edge.frame;out.seq=code.payload.k;out.timecode=true;out.source_id=code.payload.source_id;
            out.event_time_ns=quantize_timecode_ns(code.payload.tk);out.estimated_source=code.payload.estimated_source;
            if(code.payload.mode=="LIVE"){out.have_set_time=true;out.set_tai_ns=code.payload.tk;}
            ready_.push_back(std::move(out));emitted_seq_=code.payload.k;edge.emitted=true;++diagnostics_.emitted;break;
        }
    }
}
std::vector<VideoObservation> QrFlashTracker::take_events() { auto out=std::move(ready_);ready_.clear();return out; }

} // namespace avsync
