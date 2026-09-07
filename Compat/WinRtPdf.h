#pragma once

// The MinGW SDK does not currently ship windows.data.pdf.h.  This file contains
// only the stable WinRT ABI surface used by ArtThumb.  The interface identifiers
// and method order are defined by the Windows.Data.Pdf contract.

#include <inspectable.h>

namespace ABI::Windows::Storage {
struct IStorageFile;
}

namespace ABI::Windows::Storage::Streams {

MIDL_INTERFACE("905A0FE1-BC53-11DF-8C49-001E4FC686DA")
IRandomAccessStream : public IInspectable {
};

} // namespace ABI::Windows::Storage::Streams

namespace ABI::Windows::Foundation {

struct Rect {
    FLOAT X;
    FLOAT Y;
    FLOAT Width;
    FLOAT Height;
};

struct Size {
    FLOAT Width;
    FLOAT Height;
};

MIDL_INTERFACE("5A648006-843A-4DA9-865B-9D26E5DFAD7B")
IAsyncAction : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown** handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults() = 0;
};

} // namespace ABI::Windows::Foundation

namespace ABI::Windows::UI {

struct Color {
    BYTE A;
    BYTE R;
    BYTE G;
    BYTE B;
};

} // namespace ABI::Windows::UI

namespace ABI::Windows::Data::Pdf {

struct IPdfDocument;
struct IPdfPage;

MIDL_INTERFACE("d6b166ec-099a-5ee2-ad2e-f4c88614aabb")
IAsyncOperationPdfDocument : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown** handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(IPdfDocument** results) = 0;
};

MIDL_INTERFACE("AC7EBEDD-80FA-4089-846E-81B77FF5A86C")
IPdfDocument : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE GetPage(UINT32 pageIndex, IPdfPage** pdfPage) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PageCount(UINT32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPasswordProtected(boolean* value) = 0;
};

MIDL_INTERFACE("433A0B5F-C007-4788-90F2-08143D922599")
IPdfDocumentStatics : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE LoadFromFileAsync(
        ABI::Windows::Storage::IStorageFile* file,
        IAsyncOperationPdfDocument** asyncInfo) = 0;
    virtual HRESULT STDMETHODCALLTYPE LoadFromFileWithPasswordAsync(
        ABI::Windows::Storage::IStorageFile* file, HSTRING password,
        IAsyncOperationPdfDocument** asyncInfo) = 0;
    virtual HRESULT STDMETHODCALLTYPE LoadFromStreamAsync(
        ABI::Windows::Storage::Streams::IRandomAccessStream* inputStream,
        IAsyncOperationPdfDocument** asyncInfo) = 0;
    virtual HRESULT STDMETHODCALLTYPE LoadFromStreamWithPasswordAsync(
        ABI::Windows::Storage::Streams::IRandomAccessStream* inputStream,
        HSTRING password, IAsyncOperationPdfDocument** asyncInfo) = 0;
};

MIDL_INTERFACE("3C98056F-B7CF-4C29-9A04-52D90267F425")
IPdfPageRenderOptions : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE get_SourceRect(ABI::Windows::Foundation::Rect* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_SourceRect(ABI::Windows::Foundation::Rect value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DestinationWidth(UINT32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_DestinationWidth(UINT32 value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DestinationHeight(UINT32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_DestinationHeight(UINT32 value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_BackgroundColor(ABI::Windows::UI::Color* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_BackgroundColor(ABI::Windows::UI::Color value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsIgnoringHighContrast(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsIgnoringHighContrast(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_BitmapEncoderId(GUID* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_BitmapEncoderId(GUID value) = 0;
};

MIDL_INTERFACE("9DB4B0C8-5320-4CFC-AD76-493FDAD0E594")
IPdfPage : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE RenderToStreamAsync(
        ABI::Windows::Storage::Streams::IRandomAccessStream* outputStream,
        ABI::Windows::Foundation::IAsyncAction** asyncInfo) = 0;
    virtual HRESULT STDMETHODCALLTYPE RenderWithOptionsToStreamAsync(
        ABI::Windows::Storage::Streams::IRandomAccessStream* outputStream,
        IPdfPageRenderOptions* options,
        ABI::Windows::Foundation::IAsyncAction** asyncInfo) = 0;
    virtual HRESULT STDMETHODCALLTYPE PreparePageAsync(
        ABI::Windows::Foundation::IAsyncAction** asyncInfo) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Index(UINT32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(ABI::Windows::Foundation::Size* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Dimensions(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Rotation(INT32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PreferredZoom(FLOAT* value) = 0;
};

} // namespace ABI::Windows::Data::Pdf

namespace artthumb::winrt_pdf {

inline constexpr IID kIidRandomAccessStream = {
    0x905a0fe1, 0xbc53, 0x11df, {0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda}
};
inline constexpr IID kIidPdfDocumentStatics = {
    0x433a0b5f, 0xc007, 0x4788, {0x90, 0xf2, 0x08, 0x14, 0x3d, 0x92, 0x25, 0x99}
};
inline constexpr IID kIidPdfPageRenderOptions = {
    0x3c98056f, 0xb7cf, 0x4c29, {0x9a, 0x04, 0x52, 0xd9, 0x02, 0x67, 0xf4, 0x25}
};

} // namespace artthumb::winrt_pdf
