#include "Decoders/SvgRenderer.h"

#include "Decoders/WicImage.h"
#include "Provider/ComPtr.h"

#include <d2d1_3.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <shlwapi.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace artthumb {
namespace {

constexpr uint64_t kMaximumSvgBytes = 32ull * 1024ull * 1024ull;
constexpr size_t kMaximumXmlProbeBytes = 64 * 1024;

bool IsXmlSpace(char value) noexcept {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

size_t FindSequence(const std::string& text, size_t start,
                    const char* sequence, size_t length) noexcept {
    for (size_t position = start; position + length <= text.size(); ++position) {
        if (std::memcmp(text.data() + position, sequence, length) == 0) return position;
    }
    return std::string::npos;
}

std::string MakeXmlProbe(const std::vector<uint8_t>& bytes) {
    std::string probe;
    probe.reserve(std::min(bytes.size(), kMaximumXmlProbeBytes));

    size_t offset = 0;
    bool littleEndianUtf16 = false;
    bool bigEndianUtf16 = false;
    if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf) {
        offset = 3;
    } else if (bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe) {
        offset = 2;
        littleEndianUtf16 = true;
    } else if (bytes.size() >= 2 && bytes[0] == 0xfe && bytes[1] == 0xff) {
        offset = 2;
        bigEndianUtf16 = true;
    } else if (bytes.size() >= 2 && bytes[0] == '<' && bytes[1] == 0) {
        littleEndianUtf16 = true;
    } else if (bytes.size() >= 2 && bytes[0] == 0 && bytes[1] == '<') {
        bigEndianUtf16 = true;
    }

    const size_t limit = std::min(bytes.size(), kMaximumXmlProbeBytes);
    if (littleEndianUtf16 || bigEndianUtf16) {
        for (size_t index = offset; index + 1 < limit; index += 2) {
            const uint16_t codePoint = littleEndianUtf16
                ? static_cast<uint16_t>(bytes[index] | (bytes[index + 1] << 8))
                : static_cast<uint16_t>((bytes[index] << 8) | bytes[index + 1]);
            probe.push_back(codePoint <= 0x7f ? static_cast<char>(codePoint) : ' ');
        }
    } else {
        for (; offset < limit; ++offset) {
            const uint8_t value = bytes[offset];
            probe.push_back(value <= 0x7f ? static_cast<char>(value) : ' ');
        }
    }
    return probe;
}

size_t SkipDeclaration(const std::string& xml, size_t start) noexcept {
    char quote = 0;
    unsigned int subsetDepth = 0;
    for (size_t index = start; index < xml.size(); ++index) {
        const char value = xml[index];
        if (quote != 0) {
            if (value == quote) quote = 0;
            continue;
        }
        if (value == '\'' || value == '"') {
            quote = value;
        } else if (value == '[') {
            ++subsetDepth;
        } else if (value == ']' && subsetDepth != 0) {
            --subsetDepth;
        } else if (value == '>' && subsetDepth == 0) {
            return index + 1;
        }
    }
    return std::string::npos;
}

bool ParseNumber(const std::string& value, size_t& position, double& number) noexcept {
    while (position < value.size() &&
           (IsXmlSpace(value[position]) || value[position] == ',')) ++position;
    if (position >= value.size()) return false;
    char* end = nullptr;
    number = std::strtod(value.c_str() + position, &end);
    if (end == value.c_str() + position || !std::isfinite(number)) return false;
    position = static_cast<size_t>(end - value.c_str());
    return true;
}

bool ParseViewBox(const std::string& value, double& width, double& height) noexcept {
    size_t position = 0;
    double x = 0;
    double y = 0;
    return ParseNumber(value, position, x) && ParseNumber(value, position, y) &&
           ParseNumber(value, position, width) && ParseNumber(value, position, height) &&
           width > 0 && height > 0;
}

bool ParseLength(const std::string& value, double& pixels) {
    size_t position = 0;
    if (!ParseNumber(value, position, pixels)) return false;
    while (position < value.size() && IsXmlSpace(value[position])) ++position;
    const std::string unit = value.substr(position);
    double factor = 1.0;
    if (unit.empty() || unit == "px") factor = 1.0;
    else if (unit == "pt") factor = 96.0 / 72.0;
    else if (unit == "pc") factor = 16.0;
    else if (unit == "in") factor = 96.0;
    else if (unit == "cm") factor = 96.0 / 2.54;
    else if (unit == "mm") factor = 96.0 / 25.4;
    else return false;
    pixels *= factor;
    return std::isfinite(pixels) && pixels > 0;
}

bool ReadSvgCanvasSize(const std::vector<uint8_t>& bytes,
                       double& width, double& height) {
    const std::string xml = MakeXmlProbe(bytes);
    size_t position = 0;
    std::string viewBox;
    std::string widthText;
    std::string heightText;

    while ((position = xml.find('<', position)) != std::string::npos) {
        if (position + 1 >= xml.size()) return false;
        if (xml.compare(position, 4, "<!--") == 0) {
            const size_t end = FindSequence(xml, position + 4, "-->", 3);
            if (end == std::string::npos) return false;
            position = end + 3;
            continue;
        }
        if (xml.compare(position, 2, "<?") == 0) {
            const size_t end = FindSequence(xml, position + 2, "?>", 2);
            if (end == std::string::npos) return false;
            position = end + 2;
            continue;
        }
        if (xml.compare(position, 2, "<!") == 0) {
            const size_t next = SkipDeclaration(xml, position + 2);
            if (next == std::string::npos) return false;
            position = next;
            continue;
        }

        size_t nameStart = position + 1;
        size_t nameEnd = nameStart;
        while (nameEnd < xml.size() && !IsXmlSpace(xml[nameEnd]) &&
               xml[nameEnd] != '>' && xml[nameEnd] != '/') ++nameEnd;
        if (nameEnd == nameStart || xml[nameStart] == '/') return false;
        const std::string elementName = xml.substr(nameStart, nameEnd - nameStart);
        const size_t colon = elementName.find_last_of(':');
        if (elementName.substr(colon == std::string::npos ? 0 : colon + 1) != "svg")
            return false;

        size_t tagEnd = nameEnd;
        char quote = 0;
        for (; tagEnd < xml.size(); ++tagEnd) {
            const char value = xml[tagEnd];
            if (quote != 0) {
                if (value == quote) quote = 0;
            } else if (value == '\'' || value == '"') {
                quote = value;
            } else if (value == '>') {
                break;
            }
        }
        if (tagEnd == xml.size()) return false;

        size_t attribute = nameEnd;
        while (attribute < tagEnd) {
            while (attribute < tagEnd &&
                   (IsXmlSpace(xml[attribute]) || xml[attribute] == '/')) ++attribute;
            const size_t attributeStart = attribute;
            while (attribute < tagEnd && !IsXmlSpace(xml[attribute]) &&
                   xml[attribute] != '=' && xml[attribute] != '/' && xml[attribute] != '>')
                ++attribute;
            if (attribute == attributeStart) break;
            const std::string attributeName = xml.substr(attributeStart, attribute - attributeStart);
            while (attribute < tagEnd && IsXmlSpace(xml[attribute])) ++attribute;
            if (attribute >= tagEnd || xml[attribute] != '=') continue;
            ++attribute;
            while (attribute < tagEnd && IsXmlSpace(xml[attribute])) ++attribute;
            if (attribute >= tagEnd || (xml[attribute] != '\'' && xml[attribute] != '"')) continue;
            const char delimiter = xml[attribute++];
            const size_t valueStart = attribute;
            while (attribute < tagEnd && xml[attribute] != delimiter) ++attribute;
            if (attribute >= tagEnd) return false;
            const std::string value = xml.substr(valueStart, attribute - valueStart);
            ++attribute;
            if (attributeName == "viewBox") viewBox = value;
            else if (attributeName == "width") widthText = value;
            else if (attributeName == "height") heightText = value;
        }

        if (!viewBox.empty() && ParseViewBox(viewBox, width, height)) return true;
        if (!widthText.empty() && !heightText.empty() &&
            ParseLength(widthText, width) && ParseLength(heightText, height)) return true;
        width = 300;
        height = 150;
        return true;
    }
    return false;
}

HRESULT RenderSvg(const std::vector<uint8_t>& bytes, UINT edge,
                  double canvasWidth, double canvasHeight, HBITMAP* bitmap) {
    if (!bitmap || bytes.empty() || bytes.size() > UINT_MAX) return E_INVALIDARG;
    *bitmap = nullptr;

    const double scale = static_cast<double>(edge) / std::max(canvasWidth, canvasHeight);
    const UINT width = std::max<UINT>(1, static_cast<UINT>(std::lround(canvasWidth * scale)));
    const UINT height = std::max<UINT>(1, static_cast<UINT>(std::lround(canvasHeight * scale)));

    ComPtr<IStream> input(SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size())));
    if (!input) return E_OUTOFMEMORY;

    D3D_FEATURE_LEVEL featureLevel{};
    constexpr D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
    };
    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<ID3D11DeviceContext> d3dContext;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, featureLevels,
        static_cast<UINT>(std::size(featureLevels)), D3D11_SDK_VERSION,
        d3dDevice.Put(), &featureLevel, d3dContext.Put());
    if (FAILED(hr)) return hr;

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = d3dDevice->QueryInterface(IID_PPV_ARGS(dxgiDevice.Put()));
    if (FAILED(hr)) return hr;

    D2D1_FACTORY_OPTIONS factoryOptions{};
    factoryOptions.debugLevel = D2D1_DEBUG_LEVEL_NONE;
    ComPtr<ID2D1Factory1> factory;
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory1), &factoryOptions,
        reinterpret_cast<void**>(factory.Put()));
    if (FAILED(hr)) return hr;

    ComPtr<ID2D1Device> d2dDevice;
    hr = factory->CreateDevice(dxgiDevice.Get(), d2dDevice.Put());
    if (FAILED(hr)) return hr;
    ComPtr<ID2D1DeviceContext> baseContext;
    hr = d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                        baseContext.Put());
    if (FAILED(hr)) return hr;
    ComPtr<ID2D1DeviceContext5> context;
    hr = baseContext->QueryInterface(IID_PPV_ARGS(context.Put()));
    if (FAILED(hr)) return hr;

    ComPtr<ID2D1SvgDocument> document;
    hr = context->CreateSvgDocument(input.Get(),
        D2D1_SIZE_F{static_cast<float>(width), static_cast<float>(height)},
        document.Put());
    if (FAILED(hr)) return hr;

    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> renderTexture;
    hr = d3dDevice->CreateTexture2D(&description, nullptr, renderTexture.Put());
    if (FAILED(hr)) return hr;
    ComPtr<IDXGISurface> surface;
    hr = renderTexture->QueryInterface(IID_PPV_ARGS(surface.Put()));
    if (FAILED(hr)) return hr;

    D2D1_BITMAP_PROPERTIES1 bitmapProperties{};
    bitmapProperties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bitmapProperties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bitmapProperties.dpiX = 96.0f;
    bitmapProperties.dpiY = 96.0f;
    bitmapProperties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
    ComPtr<ID2D1Bitmap1> target;
    hr = context->CreateBitmapFromDxgiSurface(surface.Get(), &bitmapProperties,
                                               target.Put());
    if (FAILED(hr)) return hr;

    context->SetTarget(target.Get());
    context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    context->BeginDraw();
    context->Clear(D2D1_COLOR_F{0, 0, 0, 0});
    context->DrawSvgDocument(document.Get());
    hr = context->EndDraw();
    context->SetTarget(nullptr);
    if (FAILED(hr)) return hr;

    D3D11_TEXTURE2D_DESC stagingDescription = description;
    stagingDescription.Usage = D3D11_USAGE_STAGING;
    stagingDescription.BindFlags = 0;
    stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> stagingTexture;
    hr = d3dDevice->CreateTexture2D(&stagingDescription, nullptr, stagingTexture.Put());
    if (FAILED(hr)) return hr;
    d3dContext->CopyResource(stagingTexture.Get(), renderTexture.Get());

    if (width > std::numeric_limits<UINT>::max() / 4 ||
        static_cast<uint64_t>(width) * height > SIZE_MAX / 4)
        return E_OUTOFMEMORY;
    const UINT stride = width * 4;
    std::vector<uint8_t> pixels(static_cast<size_t>(stride) * height);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = d3dContext->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return hr;
    if (mapped.RowPitch < stride) {
        d3dContext->Unmap(stagingTexture.Get(), 0);
        return E_FAIL;
    }
    for (UINT row = 0; row < height; ++row) {
        std::memcpy(pixels.data() + static_cast<size_t>(row) * stride,
                    static_cast<const uint8_t*>(mapped.pData) +
                        static_cast<size_t>(row) * mapped.RowPitch,
                    stride);
    }
    d3dContext->Unmap(stagingTexture.Get(), 0);
    return PixelsToBitmap(pixels.data(), width, height, bitmap);
}

} // namespace

HRESULT DecodeSvg(const StreamReader& reader, UINT edge, HBITMAP* bitmap) noexcept {
    if (!reader.IsValid() || !bitmap || edge == 0 || edge > 4096) return E_INVALIDARG;
    *bitmap = nullptr;
    try {
        if (reader.Size() == 0 || reader.Size() > kMaximumSvgBytes ||
            reader.Size() > SIZE_MAX)
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        std::vector<uint8_t> bytes;
        HRESULT hr = reader.ReadVector(0, static_cast<size_t>(reader.Size()), bytes);
        if (FAILED(hr)) return hr;
        double canvasWidth = 0;
        double canvasHeight = 0;
        if (!ReadSvgCanvasSize(bytes, canvasWidth, canvasHeight)) return E_FAIL;
        return RenderSvg(bytes, edge, canvasWidth, canvasHeight, bitmap);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

} // namespace artthumb
