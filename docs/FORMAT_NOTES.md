# Format notes

## Photoshop PSD / PSB

The parser validates the `8BPS` signature and version, then walks the bounded
Image Resources section. Resource 1036 is preferred over legacy resource 1033;
both carry a 28-byte thumbnail header followed by JPEG data. If no preview is
available, the decoder downsamples the flattened composite while reading only
the source rows needed for the requested output size. Raw and PackBits/RLE,
8-bit grayscale/RGB/CMYK composites are implemented. ZIP, ZIP-prediction, Lab,
duotone, 16-bit, and 32-bit composites require an embedded JPEG preview.

## PDF and Illustrator

The first PDF page is rendered with `Windows.Data.Pdf` on a dedicated MTA worker, avoiding WinRT async deadlocks when Explorer invokes the COM handler from an STA thread. Loading and rendering are each bounded to ten seconds, and Explorer waits at most 25 seconds for the worker. AI files saved with **Create PDF Compatible File** use this same path. If PDF rendering fails, ArtThumb searches both the beginning and end of the file for an embedded XMP/JPEG preview. Old AI files are PostScript and try the EPS preview path first, followed by the same embedded-preview fallback.

## Application badges

The installer writes an empty per-extension `TypeOverlay` value so Explorer does not add a second file-type icon over ArtThumb’s badge; the prior value is backed up and restored on uninstall. Badge scale follows the longest rendered edge (normally the size requested by Explorer) with no fixed 72-pixel cap, so cached 1024-pixel PSB panoramas remain visually consistent after Explorer scales them down. The badge is reduced only when the short edge cannot contain it. ArtThumb queries only `ASSOCSTR_EXECUTABLE` and extracts icon index 0 from that executable; it never falls back to the document type’s `DefaultIcon`.

## EPS

The decoder recognizes DOS binary EPS TIFF offsets, EPSI hexadecimal previews
at 1/2/4/8 bits per pixel, and embedded JPEG data. ArtThumb intentionally does
not interpret arbitrary PostScript programs.

## InDesign INDD

INDD is identified by its 16-byte file signature. ArtThumb searches bounded head
and tail windows for the Base64 XMP `xmpGImg:image` preview InDesign stores. The
decoder accepts the XML character references InDesign uses for Base64 line breaks
and deliberately ignores unrelated JPEG assets embedded in the document. If the
document was saved without a preview, Explorer falls back to its normal icon.
