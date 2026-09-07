#include "Decoders/PdfRenderer.h"

#include "Compat/WinRtPdf.h"
#include "Decoders/WicImage.h"
#include "Provider/ComPtr.h"
#include "Provider/Module.h"

#include <asyncinfo.h>
#include <roapi.h>
#include <shcore.h>
#include <shlwapi.h>
#include <winstring.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <thread>
#include <vector>

namespace artthumb {
namespace {

using ABI::Windows::Data::Pdf::IAsyncOperationPdfDocument;
using ABI::Windows::Data::Pdf::IPdfDocument;
using ABI::Windows::Data::Pdf::IPdfDocumentStatics;
using ABI::Windows::Data::Pdf::IPdfPage;
using ABI::Windows::Data::Pdf::IPdfPageRenderOptions;
using ABI::Windows::Foundation::IAsyncAction;
using ABI::Windows::Storage::Streams::IRandomAccessStream;

constexpr uint64_t kMaximumPdfBytes = 512ull * 1024ull * 1024ull;
constexpr DWORD kOperationTimeoutMilliseconds = 10000;
constexpr DWORD kWorkerTimeoutMilliseconds = 25000;

HRESULT WaitForAsync(IUnknown* operation, DWORD timeoutMilliseconds) noexcept {
    if (!operation) return E_INVALIDARG;
    ComPtr<IAsyncInfo> info;
    HRESULT hr = operation->QueryInterface(IID_PPV_ARGS(info.Put()));
    if (FAILED(hr)) return hr;
    const ULONGLONG started = GetTickCount64();
    for (;;) {
        AsyncStatus status = Started;
        hr = info->get_Status(&status);
        if (FAILED(hr)) return hr;
        if (status == Completed) return S_OK;
        if (status == Error) {
            HRESULT error = E_FAIL;
            return SUCCEEDED(info->get_ErrorCode(&error)) ? error : E_FAIL;
        }
        if (status == Canceled) return E_ABORT;
        if (GetTickCount64() - started >= timeoutMilliseconds) {
            info->Cancel();
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
        Sleep(5);
    }
}

class RoScope final {
public:
    RoScope() noexcept {
        result_ = RoInitialize(RO_INIT_MULTITHREADED);
        owns_ = result_ == S_OK || result_ == S_FALSE;
    }
    ~RoScope() { if (owns_) RoUninitialize(); }
    HRESULT Result() const noexcept { return result_; }
private:
    HRESULT result_ = E_FAIL;
    bool owns_ = false;
};

HRESULT MakeRuntimeClass(const wchar_t* name, HSTRING_HEADER& header,
                         HSTRING& value) noexcept {
    return WindowsCreateStringReference(name, static_cast<UINT32>(wcslen(name)),
                                        &header, &value);
}

HRESULT RenderPdfBytes(const std::vector<uint8_t>& bytes, UINT edge,
                       HBITMAP* bitmap) noexcept {
    if (bytes.empty() || !bitmap || bytes.size() > UINT_MAX) return E_INVALIDARG;
    *bitmap = nullptr;

    RoScope ro;
    if (FAILED(ro.Result())) return ro.Result();

    ComPtr<IStream> source(SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size())));
    if (!source) return E_OUTOFMEMORY;
    ComPtr<IRandomAccessStream> input;
    HRESULT hr = CreateRandomAccessStreamOverStream(source.Get(), BSOS_DEFAULT,
        winrt_pdf::kIidRandomAccessStream, input.PutVoid());
    if (FAILED(hr)) return hr;

    constexpr wchar_t documentClassName[] = L"Windows.Data.Pdf.PdfDocument";
    HSTRING_HEADER documentHeader{};
    HSTRING documentClass = nullptr;
    hr = MakeRuntimeClass(documentClassName, documentHeader, documentClass);
    if (FAILED(hr)) return hr;
    ComPtr<IPdfDocumentStatics> statics;
    hr = RoGetActivationFactory(documentClass, winrt_pdf::kIidPdfDocumentStatics,
                                statics.PutVoid());
    if (FAILED(hr)) return hr;

    ComPtr<IAsyncOperationPdfDocument> load;
    hr = statics->LoadFromStreamAsync(input.Get(), load.Put());
    if (FAILED(hr)) return hr;
    hr = WaitForAsync(load.Get(), kOperationTimeoutMilliseconds);
    if (FAILED(hr)) return hr;
    ComPtr<IPdfDocument> document;
    hr = load->GetResults(document.Put());
    if (FAILED(hr)) return hr;
    UINT32 pageCount = 0;
    hr = document->get_PageCount(&pageCount);
    if (FAILED(hr) || pageCount == 0) return E_FAIL;

    ComPtr<IPdfPage> page;
    hr = document->GetPage(0, page.Put());
    if (FAILED(hr)) return hr;
    ABI::Windows::Foundation::Size pageSize{};
    hr = page->get_Size(&pageSize);
    if (FAILED(hr) || !std::isfinite(pageSize.Width) || !std::isfinite(pageSize.Height) ||
        pageSize.Width <= 0 || pageSize.Height <= 0) return E_FAIL;
    const float scale = static_cast<float>(edge) / std::max(pageSize.Width, pageSize.Height);
    const UINT32 width = std::max<UINT32>(1,
        static_cast<UINT32>(std::lround(pageSize.Width * scale)));
    const UINT32 height = std::max<UINT32>(1,
        static_cast<UINT32>(std::lround(pageSize.Height * scale)));

    constexpr wchar_t optionsClassName[] = L"Windows.Data.Pdf.PdfPageRenderOptions";
    HSTRING_HEADER optionsHeader{};
    HSTRING optionsClass = nullptr;
    hr = MakeRuntimeClass(optionsClassName, optionsHeader, optionsClass);
    if (FAILED(hr)) return hr;
    ComPtr<IInspectable> optionsInstance;
    hr = RoActivateInstance(optionsClass, optionsInstance.Put());
    if (FAILED(hr)) return hr;
    ComPtr<IPdfPageRenderOptions> options;
    hr = optionsInstance->QueryInterface(winrt_pdf::kIidPdfPageRenderOptions,
                                         options.PutVoid());
    if (FAILED(hr)) return hr;
    hr = options->put_DestinationWidth(width);
    if (FAILED(hr)) return hr;
    hr = options->put_DestinationHeight(height);
    if (FAILED(hr)) return hr;

    ComPtr<IStream> pngStream;
    hr = CreateStreamOnHGlobal(nullptr, TRUE, pngStream.Put());
    if (FAILED(hr)) return hr;
    ComPtr<IRandomAccessStream> output;
    hr = CreateRandomAccessStreamOverStream(pngStream.Get(), BSOS_DEFAULT,
        winrt_pdf::kIidRandomAccessStream, output.PutVoid());
    if (FAILED(hr)) return hr;
    ComPtr<IAsyncAction> render;
    hr = page->RenderWithOptionsToStreamAsync(output.Get(), options.Get(), render.Put());
    if (FAILED(hr)) return hr;
    hr = WaitForAsync(render.Get(), kOperationTimeoutMilliseconds);
    if (FAILED(hr)) return hr;
    hr = render->GetResults();
    if (FAILED(hr)) return hr;
    return DecodeEncodedStream(pngStream.Get(), edge, bitmap);
}

struct RenderState final {
    RenderState() noexcept : completed(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~RenderState() {
        if (bitmap) DeleteObject(bitmap);
        if (completed) CloseHandle(completed);
    }
    HANDLE completed = nullptr;
    HRESULT result = E_FAIL;
    HBITMAP bitmap = nullptr;
};

} // namespace

HRESULT DecodePdfFirstPage(const StreamReader& reader, UINT edge, HBITMAP* bitmap) noexcept {
    if (!reader.IsValid() || !bitmap || edge == 0) return E_INVALIDARG;
    *bitmap = nullptr;
    try {
        if (reader.Size() == 0 || reader.Size() > kMaximumPdfBytes || reader.Size() > SIZE_MAX)
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        auto bytes = std::make_shared<std::vector<uint8_t>>();
        HRESULT hr = reader.ReadVector(0, static_cast<size_t>(reader.Size()), *bytes);
        if (FAILED(hr)) return hr;
        auto state = std::make_shared<RenderState>();
        if (!state->completed) return HRESULT_FROM_WIN32(GetLastError());

        ObjectCreated();
        std::thread worker;
        try {
            worker = std::thread([bytes = std::move(bytes), state, edge]() noexcept {
                try {
                    state->result = RenderPdfBytes(*bytes, edge, &state->bitmap);
                } catch (const std::bad_alloc&) {
                    state->result = E_OUTOFMEMORY;
                } catch (...) {
                    state->result = E_FAIL;
                }
                SetEvent(state->completed);
                ObjectDestroyed();
            });
        } catch (...) {
            ObjectDestroyed();
            return E_OUTOFMEMORY;
        }

        const DWORD wait = WaitForSingleObject(state->completed, kWorkerTimeoutMilliseconds);
        if (wait != WAIT_OBJECT_0) {
            worker.detach();
            return wait == WAIT_TIMEOUT ? HRESULT_FROM_WIN32(ERROR_TIMEOUT)
                                        : HRESULT_FROM_WIN32(GetLastError());
        }
        worker.join();
        hr = state->result;
        if (SUCCEEDED(hr) && state->bitmap) {
            *bitmap = state->bitmap;
            state->bitmap = nullptr;
        }
        return hr;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

} // namespace artthumb
