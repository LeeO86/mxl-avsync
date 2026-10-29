#include "picture.hpp"
#include "ReadBarcode.h"
#include <algorithm>

namespace avsync {
std::optional<QrDetection> decode_qr_signal(const uint16_t* luma,int width,int height,int stride) {
    if(!luma||width<16||height<16||stride<width)return std::nullopt;
    // Bound search cost; retain enough module pixels for small camera targets.
    const int step=std::max(1,std::max(width,height)/1920);
    const int w=width/step,h=height/step;
    std::vector<uint8_t> gray(size_t(w)*h);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)gray[size_t(y)*w+x]=uint8_t(std::min(255,luma[size_t(y*step)*stride+x*step]>>2));
    ZXing::ReaderOptions options;options.setFormats(ZXing::BarcodeFormat::QRCode);options.setTextMode(ZXing::TextMode::Plain);
    for(const auto& code:ZXing::ReadBarcodes({gray.data(),w,h,ZXing::ImageFormat::Lum},options)) {
        if(!code.isValid())continue;
        auto payload=parse_qr_payload(code.text());if(!payload)continue;
        QrDetection out;out.payload=*payload;
        auto pos=code.position();auto tl=pos.topLeft(),tr=pos.topRight(),br=pos.bottomRight(),bl=pos.bottomLeft();
        const decltype(tl) corners[]={tl,tr,br,bl};
        for(int i=0;i<4;++i){out.x[i]=double(corners[i].x)*step/width;out.y[i]=double(corners[i].y)*step/height;}
        return out;
    }
    return std::nullopt;
}

std::optional<std::string> decode_qr_text(const uint16_t* luma, int width, int height, int stride)
{
    if(auto signal=decode_qr_signal(luma,width,height,stride);signal&&signal->payload.qr_flash)return qr_payload_text(signal->payload);
    const FrameGeometry g = frame_geometry(width, height);
    const int margin = std::max(8, g.qr_size / 8);
    const int x0 = std::max(0, g.qr_x - margin);
    const int y0 = std::max(0, g.qr_y - margin);
    const int x1 = std::min(width, g.qr_x + g.qr_size + margin);
    const int y1 = std::min(height, g.qr_y + g.qr_size + margin);
    ZXing::ReaderOptions options;
    options.setFormats(ZXing::BarcodeFormat::QRCode);
    options.setTextMode(ZXing::TextMode::Plain);
    const auto decode_region = [&](int left, int top, int w, int h) -> std::optional<std::string> {
        if (w < 16 || h < 16) return std::nullopt;
        std::vector<uint8_t> gray(static_cast<size_t>(w) * static_cast<size_t>(h));
        for (int y = 0; y < h; ++y) {
            const uint16_t* src = luma + static_cast<size_t>(top + y) * static_cast<size_t>(stride) + left;
            uint8_t* dst = gray.data() + static_cast<size_t>(y) * static_cast<size_t>(w);
            for (int x = 0; x < w; ++x) {
                dst[x] = static_cast<uint8_t>(std::min(255, src[x] >> 2));
            }
        }
        const ZXing::ImageView view(gray.data(), w, h, ZXing::ImageFormat::Lum);
        const ZXing::Barcode barcode = ZXing::ReadBarcode(view, options);
        if (!barcode.isValid() || barcode.text().empty()) return std::nullopt;
        return barcode.text();
    };
    if (auto text = decode_region(x0, y0, x1 - x0, y1 - y0)) return text;
    // A scaled/padded multiviewer tile does not share the canvas QR geometry.
    // Keep its original pixels rather than depending on a second rectification.
    if (auto text = decode_region(0, 0, width, height)) return text;
    // Very small resampled modules can defeat the default local binarizer.
    // Retry the same original pixels with ZXing's global histogram reader.
    options.setBinarizer(ZXing::Binarizer::GlobalHistogram);
    return decode_region(0, 0, width, height);
}

} // namespace avsync
