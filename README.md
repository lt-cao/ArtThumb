# ArtThumb 1.2.4

Created by **Cao Le**.

Official source and releases: <https://github.com/lt-cao/ArtThumb>

ArtThumb is a native Windows Explorer thumbnail provider for creative-document
formats. Windows loads the provider only when Explorer needs a thumbnail that is
not already in its cache. The thumbnail bitmap contains only the document
preview. Explorer displays the associated application's icon separately as its
native thumbnail overlay; ArtThumb does not paint an app icon into the image.

## Update checker

Open **ArtThumb Update** from the Start menu or run `ArtThumbUpdate.exe`. It checks
the latest GitHub Release when opened and offers a button to check again. If a
release is found, use **Open GitHub download page** to view it. The updater does
not download or install updates automatically. It runs only when opened and does
not stay in the background.

## Supported formats

| Format | Thumbnail source |
|---|---|
| PSD / PSB | Full-resolution 8-bit grayscale, RGB, or CMYK flattened composite first for raw or PackBits/RLE files; embedded Photoshop JPEG preview fallback |
| PDF | First page rendered by the Windows `Windows.Data.Pdf` API on a dedicated MTA worker |
| AI | First page when saved with **Create PDF Compatible File**; embedded XMP/JPEG or EPS preview fallback |
| EPS | Embedded TIFF, EPSI (`%%BeginPreview`), or JPEG preview |
| INDD | XMP JPEG page preview saved by InDesign; unrelated placed JPEG assets are ignored |
| SVG | Vector artwork rendered with Windows Direct2D on Windows 10 version 1703 or newer; embedded base64 images are supported, remote image references are not |

Some document formats can legally omit their preview. In that case ArtThumb
returns control to Explorer, which displays the normal file icon. In particular:

- Photoshop files should be saved with **Maximize PSD and PSB File Compatibility**.
- Illustrator files should be saved with **Create PDF Compatible File**. For AI files saved without PDF-compatible content, ArtThumb uses the Illustrator XMP preview when available.
- InDesign should have **Always Save Preview Images With Documents** enabled.
- SVG rendering follows the Windows Direct2D SVG feature subset; unsupported SVG elements may be omitted from the thumbnail.
- A pure vector EPS with no embedded preview cannot be rendered without a PostScript interpreter. ArtThumb deliberately does not bundle Ghostscript, keeping the provider small and avoiding a persistent or heavyweight runtime.

## Install

Requirements: 64-bit Windows 10 or Windows 11.

1. Extract the complete release ZIP.
2. Keep `ArtThumbSetup.exe`, `ArtThumbProvider.dll`, and `ArtThumbUpdate.exe` in the same folder.
3. Run `ArtThumbSetup.exe`.
4. Open a folder in File Explorer and select Medium, Large, or Extra large icons.

Installation is per-user under `%LOCALAPPDATA%\Programs\ArtThumb` and does not
request administrator rights. Upgrading from older versions removes the
ArtThumb Settings shortcut and executable and creates an **ArtThumb Update**
shortcut instead. The thumbnail sharpness control has been removed; ArtThumb
returns the source preview without an added sharpening filter and uses the
existing high-quality scaling path where resizing is needed.

The installer saves any existing per-user thumbnail-handler and `TypeOverlay`
values and restores them on uninstall. While installed, it configures Explorer's
native `TypeOverlay` association to use the associated application's file icon
when available. This is separate from the thumbnail bitmap.

To uninstall, open **Settings > Apps > Installed apps > ArtThumb > Uninstall**.

## Resource behavior

- No process remains running after installation or after the updater is closed.
- No startup entry, scheduled task, Windows service, shell watcher, or polling loop.
- The updater contacts GitHub only when the user opens it; it does not download or install releases.
- The provider reads only the file for which Explorer requests a thumbnail.
- SVG files are rasterized only on a cache miss; input is capped at 32 MiB and the returned bitmap is capped at 4096 pixels on its longest edge.
- Reads bounded regions for embedded previews (up to 16 MiB from the beginning and end), rather than scanning an entire large INDD/PSB file.
- PDF-compatible AI and PDF rendering runs on a short-lived MTA worker only on Explorer cache misses. This keeps WinRT work out of Explorer's STA COM thread and bounds the host wait to 25 seconds.
- The handler is registered with `IInitializeWithStream`; Windows may host it in an isolated shell surrogate.

## Build from source

The project is C++20 and uses Win32, COM, WIC, Windows Runtime, Direct2D, and
Direct3D WARP APIs. No third-party runtime is required.

With CMake and a Windows x64 compiler:

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Cross-compile with LLVM-MinGW:

```text
LLVM_MINGW_ROOT=/path/to/llvm-mingw ./scripts/build-cross.sh
```

## Architecture

Explorer resolves the per-extension thumbnail association to one COM class. The
provider receives an `IStream`, detects the format from file signatures and the
SVG XML root (including PDF-compatible AI), extracts or renders one preview,
scales through WIC where applicable, and returns a clean 32-bit DIB to Explorer.
It does not start Adobe applications.

See `docs/FORMAT_NOTES.md` for decoder boundaries and `docs/SECURITY.md` for the
threat model.
