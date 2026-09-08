# ArtThumb 1.2.1

Released by **Cao Le**.

## Changes

- Removed all `Ps`, `Ai`, `Id`, and `PDF` badges from Explorer thumbnails and the settings preview.
- Kept Explorer's native `TypeOverlay` suppressed for supported formats, preventing a second corner icon.
- PSD and PSB now prefer the full-resolution flattened composite over the small embedded JPEG preview.
- Embedded previews use WIC high-quality cubic scaling at Explorer's requested thumbnail size.
- Simplified ArtThumb Settings to a clean preview and one thumbnail-sharpness control.

## Install

1. Download `ArtThumb-1.2.1-windows-x64.zip`.
2. Extract all three files.
3. Run `ArtThumbSetup.exe` and reopen File Explorer if an old cached thumbnail is still visible.
