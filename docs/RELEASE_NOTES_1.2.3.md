# ArtThumb 1.2.3

Released by **Cao Le**.

## Changes

- Adds native SVG thumbnail rendering in Windows Explorer using Direct2D.
- Supports SVG documents with UTF-8/UTF-16 XML, an `svg` root element, and embedded base64 images; remote image references are not fetched.
- Bounds SVG input to 32 MiB and thumbnail rendering to a 4096-pixel longest edge.
- Keeps the document preview clean; Explorer applies the associated application's native icon separately.

SVG rendering requires Windows 10 version 1703 or newer. Unsupported SVG features or documents without renderable content may fall back to the normal file icon.

## Install

1. Download `ArtThumb-1.2.3-windows-x64.zip`.
2. Extract all three files.
3. Run `ArtThumbSetup.exe` and reopen File Explorer if an old cached thumbnail is still visible.
