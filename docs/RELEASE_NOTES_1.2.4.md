# ArtThumb 1.2.4

## Highlights

- Replaced ArtThumb Settings with a standalone **ArtThumb Update** app that checks GitHub Releases when opened and links to the release page. It does not download or install updates.
- Removed the thumbnail preview and sharpness controls; thumbnails no longer receive an optional sharpening filter.
- Installer now creates an **ArtThumb Update** Start Menu shortcut and removes the old Settings shortcut, executable, and registry path when upgrading.
- AI files saved without PDF-compatible content now use their embedded Illustrator XMP preview when available instead of showing Illustrator's compatibility warning page.
- Kept Explorer's associated application icon as a native thumbnail overlay, separate from the document preview bitmap.
- Continues to support PSD, PSB, AI, EPS, INDD, PDF, and SVG.

## Windows installation

Download `ArtThumb-1.2.4-windows-x64.zip`, extract the complete archive, and run
`ArtThumbSetup.exe`. Keep `ArtThumbProvider.dll` and `ArtThumbUpdate.exe` beside
the installer while installing.

Requirements: 64-bit Windows 10 or Windows 11.
