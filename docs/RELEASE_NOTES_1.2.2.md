# ArtThumb 1.2.2

Released by **Cao Le**.

## Changes

- Explorer applies the associated application's native icon as a separate thumbnail overlay.
- The thumbnail provider returns only document pixels and does not paint application icons into the image.
- ArtThumb Settings shows the app icon as a separate preview layer.
- Installer migration removes the empty `TypeOverlay` override left by 1.2.1 and restores the previous value on uninstall.

## Install

1. Download `ArtThumb-1.2.2-windows-x64.zip`.
2. Extract all three files.
3. Run `ArtThumbSetup.exe` and reopen File Explorer if an old cached thumbnail is still visible.
