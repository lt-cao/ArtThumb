#include "Decoders/WicImage.h"

#include "Provider/ComPtr.h"

#include <shlwapi.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace artthumb {
namespace {

HRESULT CreateFactory(ComPtr<IWICImagingFactory>& factory) noexcept {
    return CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                            IID_PPV_ARGS(factory.Put()));
}

} // namespace

HRESULT PixelsToBitmap(const uint8_t* bgra, UINT width, UINT height,
                       HBITMAP* bitmap) noexcept {
    if (!bgra || !bitmap || width == 0 || height == 0) return E_INVALIDARG;
    *bitmap = nullptr;
    if (width > std::numeric_limits<UINT>::max() / 4 ||
        static_cast<uint64_t>(width) * height > SIZE_MAX / 4) return E_OUTOFMEMORY;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    info.bmiHeader.biHeight = -static_cast<LONG>(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP dib = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!dib || !pixels) return HRESULT_FROM_WIN32(GetLastError());
    std::memcpy(pixels, bgra, static_cast<size_t>(width) * height * 4);
    *bitmap = dib;
    return S_OK;
}

HRESULT DecodeEncodedStream(IStream* stream, UINT edge, HBITMAP* bitmap) noexcept {
    if (!stream || !bitmap || edge == 0) return E_INVALIDARG;
    *bitmap = nullptr;
    try {
        LARGE_INTEGER zero{};
        HRESULT hr = stream->Seek(zero, STREAM_SEEK_SET, nullptr);
        if (FAILED(hr)) return hr;

        ComPtr<IWICImagingFactory> factory;
        hr = CreateFactory(factory);
        if (FAILED(hr)) return hr;
        ComPtr<IWICBitmapDecoder> decoder;
        hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand,
                                               decoder.Put());
        if (FAILED(hr)) return hr;
        ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, frame.Put());
        if (FAILED(hr)) return hr;

        UINT sourceWidth = 0;
        UINT sourceHeight = 0;
        hr = frame->GetSize(&sourceWidth, &sourceHeight);
        if (FAILED(hr) || sourceWidth == 0 || sourceHeight == 0) return E_FAIL;
        const double scale = std::min(1.0, static_cast<double>(edge) /
                                           std::max(sourceWidth, sourceHeight));
        const UINT width = std::max<UINT>(1, static_cast<UINT>(std::lround(sourceWidth * scale)));
        const UINT height = std::max<UINT>(1, static_cast<UINT>(std::lround(sourceHeight * scale)));

        ComPtr<IWICBitmapSource> source;
        if (width != sourceWidth || height != sourceHeight) {
            ComPtr<IWICBitmapScaler> scaler;
            hr = factory->CreateBitmapScaler(scaler.Put());
            if (FAILED(hr)) return hr;
            hr = scaler->Initialize(frame.Get(), width, height, WICBitmapInterpolationModeFant);
            if (FAILED(hr)) return hr;
            hr = scaler->QueryInterface(IID_PPV_ARGS(source.Put()));
        } else {
            hr = frame->QueryInterface(IID_PPV_ARGS(source.Put()));
        }
        if (FAILED(hr)) return hr;

        ComPtr<IWICFormatConverter> converter;
        hr = factory->CreateFormatConverter(converter.Put());
        if (FAILED(hr)) return hr;
        hr = converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) return hr;

        const UINT stride = width * 4;
        std::vector<uint8_t> pixels(static_cast<size_t>(stride) * height);
        hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data());
        if (FAILED(hr)) return hr;
        return PixelsToBitmap(pixels.data(), width, height, bitmap);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
}

HRESULT DecodeEncodedImage(const uint8_t* bytes, size_t length, UINT edge,
                           HBITMAP* bitmap) noexcept {
    if (!bytes || length == 0 || length > std::numeric_limits<UINT>::max()) return E_INVALIDARG;
    ComPtr<IStream> stream(SHCreateMemStream(bytes, static_cast<UINT>(length)));
    if (!stream) return E_OUTOFMEMORY;
    return DecodeEncodedStream(stream.Get(), edge, bitmap);
}

} // namespace artthumb
