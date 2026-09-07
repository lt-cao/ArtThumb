# Security and privacy

Thumbnail handlers parse untrusted files inside Explorer's thumbnail pipeline.
ArtThumb therefore uses stream-based initialization, validates section lengths
before allocation, bounds preview searches and embedded payload size, rejects
unsupported encodings, and does not disable Windows process isolation.

The provider does not use the network, execute document content, invoke Adobe
applications, run PostScript, load plug-ins, or write document data. The separate
settings executable performs one bounded HTTPS request to GitHub's releases API
only when the user clicks **Check for updates**; there is no automatic check,
downloader, or background updater. The only local writes are installer/settings
registry values and files under the current user's
`%LOCALAPPDATA%\Programs\ArtThumb` directory.

The release is not code-signed. Windows may show a SmartScreen warning for a
downloaded build. Review the source and build locally if code signing is required.
