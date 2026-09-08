# ArtThumb 1.2.1

Created by **Cao Le**.

Official source and releases: <https://github.com/lt-cao/ArtThumb>

ArtThumb is a native Windows Explorer thumbnail provider for creative-document
formats. Windows loads the provider only when Explorer needs a thumbnail that is
not already in its cache. The separate native **ArtThumb Settings** window shows a
clean live preview and lets the user adjust thumbnail sharpness.

Each thumbnail contains only the document preview. ArtThumb never draws `Ps`, `Ai`,
`Id`, or `PDF` over the content. The installer also suppresses Explorer’s built-in
file-type overlay for the six supported extensions, so no second icon is added in a
corner by Windows.

## Settings window

Open **ArtThumb Settings** from the Start menu. It provides:

- A live File Explorer-style preview for PSD, PSB, AI, EPS, INDD, and PDF.
- Optional thumbnail sharpening from 0% to 100%; 0% preserves original pixels.
- A format-support summary and author/version information.
- A manual GitHub update check against `lt-cao/ArtThumb` releases.

Settings are stored as one small per-user registry value under
`HKCU\Software\ArtThumb\Settings`. The provider reads them only on Explorer cache
misses. The settings app never stays in the background and contacts GitHub only
when **Check for updates** is clicked.

## Supported formats

| Format | Thumbnail source |
|---|---|
| PSD / PSB | Full-resolution 8-bit grayscale, RGB, or CMYK flattened composite first for raw or PackBits/RLE files; embedded Photoshop JPEG preview fallback |
| PDF | First page rendered by the Windows `Windows.Data.Pdf` API on a dedicated MTA worker |
| AI | First page when saved with **Create PDF Compatible File**; embedded XMP/JPEG or EPS preview fallback |
| EPS | Embedded TIFF, EPSI (`%%BeginPreview`), or JPEG preview |
| INDD | XMP JPEG page preview saved by InDesign; unrelated placed JPEG assets are ignored |

Some document formats can legally omit their preview. In that case ArtThumb
returns control to Explorer, which displays the normal file icon. In particular:

- Photoshop files should be saved with **Maximize PSD and PSB File Compatibility**.
- Illustrator files should be saved with **Create PDF Compatible File**.
- InDesign should have **Always Save Preview Images With Documents** enabled.
- A pure vector EPS with no embedded preview cannot be rendered without a
  PostScript interpreter. ArtThumb deliberately does not bundle Ghostscript,
  keeping the provider small and avoiding a persistent or heavyweight runtime.

## Install

Requirements: 64-bit Windows 10 or Windows 11.

1. Extract the complete release ZIP.
2. Keep `ArtThumbSetup.exe`, `ArtThumbProvider.dll`, and `ArtThumbSettings.exe`
   in the same folder.
3. Run `ArtThumbSetup.exe`.
4. Open a folder in File Explorer and select Medium, Large, or Extra large icons.

Installation is per-user under `%LOCALAPPDATA%\Programs\ArtThumb` and does not
request administrator rights. The installer saves any existing per-user
thumbnail-handler and `TypeOverlay` values and restores them on uninstall.

To uninstall, open **Settings > Apps > Installed apps > ArtThumb > Uninstall**.

## Resource behavior

- No process remains running after installation.
- No startup entry, scheduled task, Windows service, shell watcher, or polling loop.
- No automatic network check; the settings window contacts the GitHub API only
  after an explicit button click.
- Reads only the file for which Explorer requests a thumbnail.
- Reads bounded regions for embedded previews (up to 16 MiB from the beginning
  and end), rather than scanning an entire large INDD/PSB file.
- PDF-compatible AI and PDF rendering runs on a short-lived MTA worker only on Explorer cache misses. This keeps WinRT work out of Explorer’s STA COM thread and bounds the host wait to 25 seconds.
- The handler is registered with `IInitializeWithStream`; Windows may host it in
  an isolated shell surrogate.

## Build from source

The project is C++20 and uses Win32, COM, WIC, and Windows Runtime APIs only.
No third-party runtime is required.

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
provider receives an `IStream`, detects the format from file signatures (including
PDF-compatible AI), extracts or renders one preview, scales through WIC, applies
the selected sharpness, and returns a clean 32-bit DIB to Explorer. It does not
start Adobe applications.

See `docs/FORMAT_NOTES.md` for decoder boundaries and `docs/SECURITY.md` for the
threat model.
