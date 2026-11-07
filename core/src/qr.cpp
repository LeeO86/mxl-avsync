#include "avsync/avsync.hpp"
#include "../../third_party/nayuki/qrcodegen.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
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
// AVS4 layout (all integers big-endian), sent as "AVS4:" + base45 (RFC 9285) of these bytes:
//   0      flags: bit0 LIVE, bit1 phone clock (clk=phone-utc), bit2 rate 2 (else 1),
//          bit3 calibration block follows, bits4-7 format index
//   1..5   k, event sequence (40 bits)
//   6..13  T_k, source time of event k in ns
//   14..16 test A/V offset in 1/16 ms (signed 24 bits)
//   [6 bytes, only with bit3] calibration method (1 clap, 2 transponder, 3 pulse-train),
//          offset in 0.1 ms (signed 16 bits), uncertainty in 0.5 ms (8 bits), day since 2020-01-01 (16 bits, 0 unknown)
//   1 byte n, then the test ID: n characters of 0-9a-zA-Z-_ at 6 bits each (source ID = CRC-32 of the ID),
//          or for n = 0 the 4-byte source ID alone (the analyser shows "source-<hex>")
//   rest   generator name, UTF-8, 0..24 bytes
constexpr const char* formats[] = {"1080p50", "1080p25", "1080p59.94", "1080i25", "2160p50", "720p60", "720p50", "960x540p25", "540p25"};
constexpr const char* methods[] = {"none", "clap", "transponder", "pulse-train"};
constexpr char base45[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";
constexpr char id_chars[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_";
constexpr char hex[] = "0123456789ABCDEF";
// Version 4 with ECC M holds 90 alphanumeric characters: 5 for the prefix, 85 base45 = 56 bytes.
constexpr size_t kMaxBytes = 56;

// Longest valid UTF-8 prefix of at most max bytes; stops at the first invalid sequence.
size_t utf8_prefix(std::string_view s, size_t max) {
    size_t i = 0, ok = 0;
    while (i < s.size()) {
        const auto c = uint8_t(s[i]);
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!n || (n == 1 && c < 0x20) || i + n > s.size()) break; // control characters end the name too
        uint32_t cp = n == 1 ? c : c & (0x7F >> n);
        bool valid = true;
        for (size_t j = 1; j < n; ++j) { const auto t = uint8_t(s[i + j]); valid &= (t >> 6) == 2; cp = cp << 6 | (t & 0x3F); }
        if (!valid || (n == 2 && cp < 0x80) || (n == 3 && (cp < 0x800 || (cp >= 0xD800 && cp < 0xE000))) || (n == 4 && (cp < 0x10000 || cp > 0x10FFFF))) break;
        if (i + n > max) break;
        i += n; ok = i;
    }
    return ok;
}

std::optional<QrPayload> parse_avs4(std::string_view text) {
    text.remove_prefix(5);
    if (text.size() % 3 == 1) return std::nullopt;
    std::vector<uint8_t> b;
    for (size_t i = 0; i < text.size(); i += 3) {
        uint32_t n = 0, scale = 1;
        const size_t len = std::min<size_t>(3, text.size() - i);
        for (size_t j = 0; j < len; ++j) {
            const char* at = std::char_traits<char>::find(base45, 45, text[i + j]);
            if (!at) return std::nullopt;
            n += uint32_t(at - base45) * scale; scale *= 45;
        }
        if (n > (len == 3 ? 0xFFFFu : 0xFFu)) return std::nullopt;
        if (len == 3) b.push_back(uint8_t(n >> 8));
        b.push_back(uint8_t(n));
    }
    if (b.size() < 18 || b.size() > kMaxBytes) return std::nullopt;
    size_t at = 0;
    auto read = [&](int n) { uint64_t v = 0; for (int i = 0; i < n; ++i) v = (v << 8) | b[at++]; return v; };
    const uint8_t flags = uint8_t(read(1));
    if ((flags >> 4) >= std::size(formats)) return std::nullopt;
    QrPayload p;
    p.qr_flash = p.timecode = p.optical_code = true;
    p.mode = (flags & 1) ? "LIVE" : "FILE";
    p.estimated_source = flags & 2;
    p.rate = (flags & 4) ? 2 : 1;
    p.format = formats[flags >> 4];
    if (p.estimated_source && p.mode != "LIVE") return std::nullopt;
    p.k = read(5);
    const uint64_t tk = read(8);
    if (tk > uint64_t(INT64_MAX)) return std::nullopt;
    p.tk = int64_t(tk);
    const uint32_t oq = uint32_t(read(3));
    p.offset_ms = (oq < 0x800000 ? int32_t(oq) : int32_t(oq) - 0x1000000) / 16.0;
    if (flags & 8) {
        if (b.size() - at < 7) return std::nullopt;
        const auto method = read(1);
        if (method == 0 || method >= std::size(methods)) return std::nullopt;
        p.calibration.method = methods[method];
        p.calibration.offset_ms = int16_t(uint16_t(read(2))) / 10.0;
        p.calibration.uncertainty_ms = read(1) / 2.0;
        p.calibration.day = int(read(2));
    }
    const size_t n = read(1);
    if (n > 64) return std::nullopt;
    if (n == 0) {
        if (b.size() - at < 4) return std::nullopt;
        p.source_id = uint32_t(read(4));
        p.test_id = "source-";
        for (int shift = 28; shift >= 0; shift -= 4) p.test_id += hex[(p.source_id >> shift) & 15];
    } else {
        const size_t bytes = (n * 6 + 7) / 8;
        if (b.size() - at < bytes) return std::nullopt;
        uint32_t acc = 0; int bits = 0;
        for (size_t i = 0; i < bytes; ++i) {
            acc = (acc << 8) | b[at++]; bits += 8;
            while (bits >= 6 && p.test_id.size() < n) { bits -= 6; p.test_id += id_chars[(acc >> bits) & 63]; }
        }
        if (acc & ((1u << bits) - 1)) return std::nullopt; // padding bits must be zero
        p.source_id = timecode_source_id(p.test_id.c_str());
    }
    p.name.assign(b.begin() + long(at), b.end());
    if (p.name.size() > kQrNameMaxBytes || utf8_prefix(p.name, p.name.size()) != p.name.size()) return std::nullopt;
    return p;
}
}

std::string calibration_date(int day) {
    if (day <= 0) return {};
    // Civil date from days since 1970-01-01 (H. Hinnant's algorithm); 2020-01-01 is day 18262.
    const int z = day + 18262 + 719468, era = z / 146097, doe = z - era * 146097;
    const int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    char out[16];
    std::snprintf(out, sizeof out, "%04d-%02d-%02d", yoe + era * 400 + (m <= 2), m, d);
    return out;
}

std::string qr_payload_text(const QrPayload& p) {
    if (!p.qr_flash) return qr_payload_json(p);
    const auto format = std::find(std::begin(formats), std::end(formats), p.format);
    const auto method = std::find(std::begin(methods), std::end(methods), p.calibration.method);
    const auto& cal = p.calibration;
    if (!p.timecode || !p.optical_code || format == std::end(formats) || (p.rate != 1 && p.rate != 2) || p.tk < 0 || p.k >= (1ULL << 40) ||
        (p.mode != "LIVE" && p.mode != "FILE") || !std::isfinite(p.offset_ms) || std::abs(std::llround(p.offset_ms * 16)) > 0x7FFFFF ||
        (p.estimated_source && p.mode != "LIVE") || method == std::end(methods) ||
        (cal.calibrated() && (!std::isfinite(cal.offset_ms) || std::abs(std::llround(cal.offset_ms * 10)) > 32767 ||
                              !std::isfinite(cal.uncertainty_ms) || cal.uncertainty_ms < 0 || std::llround(cal.uncertainty_ms * 2) > 255 ||
                              cal.day < 0 || cal.day > 0xFFFF)))
        throw std::invalid_argument("invalid AVS4 payload");
    std::vector<uint8_t> b;
    auto put = [&](int n, uint64_t v) { for (int i = n - 1; i >= 0; --i) b.push_back(uint8_t(v >> (8 * i))); };
    put(1, (p.mode == "LIVE" ? 1 : 0) | (p.estimated_source ? 2 : 0) | (p.rate == 2 ? 4 : 0) | (cal.calibrated() ? 8 : 0) |
               (format - std::begin(formats)) << 4);
    put(5, p.k);
    put(8, uint64_t(p.tk));
    put(3, uint32_t(int32_t(std::llround(p.offset_ms * 16))) & 0xFFFFFF);
    if (cal.calibrated()) {
        put(1, uint64_t(method - std::begin(methods)));
        put(2, uint16_t(int16_t(std::llround(cal.offset_ms * 10))));
        put(1, uint64_t(std::llround(cal.uncertainty_ms * 2)));
        put(2, uint64_t(cal.day));
    }
    // The real test ID when it fits and gives the same source ID; else the source ID alone.
    const size_t id_bytes = (p.test_id.size() * 6 + 7) / 8;
    if (!p.test_id.empty() && p.test_id.size() <= 64 && p.test_id.find_first_not_of(id_chars) == std::string::npos &&
        timecode_source_id(p.test_id.c_str()) == p.source_id && b.size() + 1 + id_bytes <= kMaxBytes) {
        put(1, p.test_id.size());
        uint32_t acc = 0; int bits = 0;
        for (char c : p.test_id) {
            acc = (acc << 6) | uint32_t(std::strchr(id_chars, c) - id_chars); bits += 6;
            if (bits >= 8) { bits -= 8; b.push_back(uint8_t(acc >> bits)); }
        }
        if (bits) b.push_back(uint8_t(acc << (8 - bits)));
    } else {
        put(1, 0);
        put(4, p.source_id);
    }
    // The name is cosmetic: cut at a character boundary to the room left.
    const size_t room = std::min(kQrNameMaxBytes, kMaxBytes - b.size());
    b.insert(b.end(), p.name.begin(), p.name.begin() + long(utf8_prefix(p.name, room)));
    std::string out = kQrCompactPrefix;
    for (size_t i = 0; i < b.size(); i += 2) {
        const uint32_t n = i + 1 < b.size() ? b[i] * 256u + b[i + 1] : b[i];
        out += base45[n % 45]; out += base45[n / 45 % 45];
        if (i + 1 < b.size()) out += base45[n / 2025];
    }
    return out;
}

std::optional<QrPayload> parse_qr_payload(std::string_view json)
{
    if(json.starts_with(kQrCompactPrefix))return parse_avs4(json);
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
    // The QR is still for half a period around each burst, so any time works.
    return !anchor_||!pose_.found||time<anchor_ms_||time-anchor_ms_>=150;
}
void QrFlashTracker::observe(const QrDetection& q,double time) {
    if(!q.payload.optical_code||!q.payload.timecode||!std::isfinite(time)||!parse_video_format(q.payload.format))return;
    std::array<double,9> h{};if(!qr_transform(q,h))return;
    if(anchor_&&time<anchor_ms_)return; // Asynchronous results retain capture order.
    if(anchor_&&!same_qr_source(q.payload,anchor_->payload))reset();
    if(anchor_){
        // A moved picture samples other pixels: restart the field history.
        double movement=0;for(int i=0;i<4;++i)movement=std::max(movement,std::hypot(q.x[i]-anchor_->x[i],q.y[i]-anchor_->y[i]));
        const double side=std::hypot(q.x[1]-q.x[0],q.y[1]-q.y[0]);
        if(movement>std::max(.005,side*.08)){optical_.reset();recent_.clear();bursts_.clear();codes_.clear();}
    }
    const bool first=!anchor_;
    anchor_=q;anchor_ms_=time;transform_=h;
    if(first)configure_optical();
    codes_.push_back({q.payload,time});
    while(codes_.size()>32||time-codes_.front().time>2000)codes_.pop_front();
    match_optical(last_frame_ms_);
}

void QrFlashTracker::configure_optical() {
    const auto format=parse_video_format(anchor_->payload.format);
    const auto period=plan_period(*format,anchor_->payload.rate);
    const double grain_ms=period.period_ms/period.grains;
    optical_period_ms_=period.period_ms;
    optical_.configure(optical_chip_grains(1000/grain_ms,anchor_->payload.rate)*grain_ms,period.period_ms);
}

// Bind each fitted burst to the QR seen within 3/8 of a period of it (the QR names
// event k from half a period before its burst to half after) with the burst's
// parity. Without such a QR, count on from the last bound burst on the event grid.
void QrFlashTracker::match_optical(double now) {
    const double period=optical_period_ms_,window=period/2-period/8;
    while(!bursts_.empty()) {
        const auto& pending=bursts_.front();const double t=pending.burst.time_ms;
        const QrPayload* code=nullptr;double best=window;
        for(const auto& c:codes_)
            if((c.payload.k&1)==uint64_t(pending.burst.variant)&&(!emitted_seq_||c.payload.k>*emitted_seq_)&&std::abs(c.time-t)<=best){best=std::abs(c.time-t);code=&c.payload;}
        QrPayload named;
        if(code)named=*code;
        else {
            if(now-t<period/2)break; // A later QR result may still name it.
            bool grid=false;
            if(grid_&&t>grid_->second&&t-grid_->second<20000) {
                const double n=(t-grid_->second)/period,r=std::round(n);
                if(r>=1&&std::abs(n-r)<=.12&&((grid_->first+uint64_t(r))&1)==uint64_t(pending.burst.variant)) {
                    const auto format=parse_video_format(anchor_->payload.format);
                    const int64_t period_ns=index_to_ns(format->grain_rate(),plan_period(*format,anchor_->payload.rate).grains);
                    named=anchor_->payload;named.k=grid_->first+uint64_t(r);
                    named.tk=anchor_->payload.tk+(int64_t(named.k)-int64_t(anchor_->payload.k))*period_ns;
                    grid=named.tk>=0;
                }
            }
            if(!grid){++diagnostics_.optical_unbound;bursts_.pop_front();continue;}
            ++diagnostics_.optical_by_grid;
        }
        if(emitted_seq_&&named.k<=*emitted_seq_){bursts_.pop_front();continue;}
        auto out=pending.frame;
        out.event=true;out.marker_white=true;out.quality="good";out.contrast=pending.burst.contrast;out.time_ms=t;
        out.flags.push_back("qr_flash");out.flags.push_back("optical_code");out.flags.push_back("frame_counter_unavailable");
        if(pending.optical)out.flags.push_back("optical_capture");
        out.seq=named.k;out.timecode=true;out.source_id=named.source_id;
        out.event_time_ns=quantize_timecode_ns(named.tk);out.estimated_source=named.estimated_source;
        if(named.mode=="LIVE"){out.have_set_time=true;out.set_tai_ns=named.tk;}
        ready_.push_back(std::move(out));emitted_seq_=named.k;grid_={named.k,t};++diagnostics_.emitted;
        bursts_.pop_front();
    }
}

void QrFlashTracker::push(const uint16_t* pixels,int width,int height,int stride,VideoObservation frame) { push_pixels(pixels,false,width,height,stride,frame); }
void QrFlashTracker::push_u8(const uint8_t* pixels,int width,int height,int stride,VideoObservation frame) { push_pixels(pixels,true,width,height,stride,frame); }
void QrFlashTracker::push_pixels(const void* pixels,bool eight_bit,int width,int height,int stride,VideoObservation frame) {
    ++diagnostics_.frames;
    valid_=false;pose_.found=false;
    if(!anchor_||!pixels||width<16||height<16||stride<width||!std::isfinite(frame.time_ms))return;
    if(frame.time_ms<=last_frame_ms_)return;
    last_frame_ms_=frame.time_ms;
    if(frame.time_ms-anchor_ms_>750||frame.time_ms-anchor_ms_<-50){++diagnostics_.geometry_rejections;return;}
    ++diagnostics_.probe_frames;
    auto sample=[&](double mx,double my)->double{
        double x,y;if(!project_qr(transform_,mx,my,x,y)||x<0||y<0||x>1||y>1)return -1;
        int ix=int(std::lround(x*width)),iy=int(std::lround(y*height));
        if(ix<0||iy<0||ix>=width||iy>=height)return -1;
        return eight_bit ? static_cast<const uint8_t*>(pixels)[size_t(iy)*stride+ix]*4 : static_cast<const uint16_t*>(pixels)[size_t(iy)*stride+ix];
    };
    // Field A is the left half of the panel ([41,61.5] in QR modules), B the right half.
    // Median of a 5x5 grid inside each half, clear of the edge between them.
    auto field=[&](double x0)->double{
        std::array<double,25> p{};int n=0;
        for(int j=0;j<5;++j)for(int i=0;i<5;++i){const double v=sample(x0+20.5*(.25+.125*i),-4+41*(.2+.15*j));if(v<0)return -1;p[n++]=v;}
        std::nth_element(p.begin(),p.begin()+12,p.end());return p[12];
    };
    const double a=field(41),b=field(61.5);
    double cx,cy;
    if(a<0||b<0||!project_qr(transform_,61.5,16.5,cx,cy)){++diagnostics_.geometry_rejections;return;}
    
    const double xs[]={-4,82,82,-4},ys[]={-4,-4,37,37};
    for(int i=0;i<4;++i){double x,y;if(!project_qr(transform_,xs[i],ys[i],x,y)){++diagnostics_.geometry_rejections;return;}pose_.corner_x[i]=x*width;pose_.corner_y[i]=y*height;}
    pose_.found=true;pose_.origin_x=pose_.corner_x[0];pose_.origin_y=pose_.corner_y[0];
    pose_.width=std::hypot(pose_.corner_x[1]-pose_.corner_x[0],pose_.corner_y[1]-pose_.corner_y[0]);
    pose_.height=std::hypot(pose_.corner_x[3]-pose_.corner_x[0],pose_.corner_y[3]-pose_.corner_y[0]);
    valid_=true;
    // Rolling shutter: the panel's rows start later than the frame's first row.
    const double start=frame.time_ms+frame.readout_ms*std::clamp(cy,0.0,1.0);
    // Digital grains (and cameras without metadata) integrate over the whole frame interval.
    double exposure=frame.exposure_ms;
    if(exposure<=0&&!recent_.empty())exposure=std::min(frame.time_ms-recent_.back().time_ms,100.0);
    optical_.push({start,std::max(0.0,exposure),a,b});
    diagnostics_.samples.push_back({frame.time_ms,(a-b)/std::max(1.0,a+b),0,a>b?2:0});
    while(diagnostics_.samples.size()>64)diagnostics_.samples.pop_front();
    recent_.push_back(frame);
    while(recent_.size()>2&&recent_.front().time_ms<frame.time_ms-2*optical_period_ms_)recent_.pop_front();
    // A camera picture: the QR is not at the generator's own raster position.
    const auto g=qr_flash_geometry(width,height);const double unit=g.qr_size/41.0;
    const double ex[]={g.qr_x+4*unit,g.qr_x+37*unit,g.qr_x+37*unit,g.qr_x+4*unit};
    const double ey[]={g.qr_y+4*unit,g.qr_y+4*unit,g.qr_y+37*unit,g.qr_y+37*unit};
    bool optical=anchor_->payload.estimated_source;
    for(int i=0;i<4;++i)optical|=std::hypot(anchor_->x[i]*width-ex[i],anchor_->y[i]*height-ey[i])>3;
    for(const auto& burst:optical_.take()) {
        // Grain fields (index, TAI, arrival) of the frame closest to transition 0.
        const VideoObservation* near=&recent_.back();
        for(const auto& f:recent_)if(std::abs(f.time_ms-burst.time_ms)<std::abs(near->time_ms-burst.time_ms))near=&f;
        bursts_.push_back({burst,*near,optical});++diagnostics_.edges;
    }
    const auto& d=optical_.diagnostics();
    diagnostics_.optical_crossings=d.crossings;diagnostics_.optical_bursts=d.bursts;diagnostics_.optical_rejected=d.rejected;
    match_optical(frame.time_ms);
}
std::vector<VideoObservation> QrFlashTracker::take_events() { auto out=std::move(ready_);ready_.clear();return out; }

} // namespace avsync
