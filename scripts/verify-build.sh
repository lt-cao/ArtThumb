#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "$0")/.." && pwd)"
toolchain_root="${LLVM_MINGW_ROOT:-}"
if [[ -z "$toolchain_root" ]]; then
  echo "Set LLVM_MINGW_ROOT to the extracted llvm-mingw directory." >&2
  exit 2
fi

reader="$toolchain_root/bin/llvm-readobj"
dll="$project_root/build-cross/ArtThumbProvider.dll"
setup="$project_root/build-cross/ArtThumbSetup.exe"
updater="$project_root/build-cross/ArtThumbUpdate.exe"
[[ -f "$dll" && -f "$setup" && -f "$updater" ]]

dll_report="$("$reader" --file-headers --coff-exports --coff-imports "$dll")"
setup_report="$("$reader" --file-headers --coff-imports --coff-resources "$setup")"
updater_report="$("$reader" --file-headers --coff-imports --coff-resources "$updater")"

rg -q 'Arch: x86_64' <<<"$dll_report"
rg -q 'IMAGE_FILE_DLL' <<<"$dll_report"
for export_name in DllCanUnloadNow DllGetClassObject DllRegisterServer DllUnregisterServer; do
  rg -q "Name: $export_name" <<<"$dll_report"
done
rg -q 'Type: MANIFEST' <<<"$setup_report"
rg -q 'Type: VERSIONINFO' <<<"$setup_report"
rg -q 'Arch: x86_64' <<<"$updater_report"
rg -q 'IMAGE_SUBSYSTEM_WINDOWS_GUI' <<<"$updater_report"
rg -q 'Type: MANIFEST' <<<"$updater_report"
rg -q 'Type: VERSIONINFO' <<<"$updater_report"
rg -q 'Name: WINHTTP.dll' <<<"$updater_report"

if rg -q 'Name: (libstdc\+\+|libgcc|libwinpthread|libc\+\+).*\.dll' \
    <<<"$dll_report$setup_report$updater_report"; then
  echo "Unexpected compiler runtime DLL dependency." >&2
  exit 1
fi

for extension in psd psb ai eps indd pdf svg; do
  rg -q "\\.$extension" "$project_root/Provider/Registration.cpp"
  rg -q "\\.$extension" "$project_root/Installer/Setup.cpp"
done
rg -q 'DecodeSvg' "$project_root/Decoders/ThumbnailPipeline.cpp"
rg -q 'HasIllustratorNoPdfContentWarning' "$project_root/Decoders/ThumbnailPipeline.cpp"
rg -q 'DecodeGenericEmbeddedPreview\(reader, edge, bitmap\)' "$project_root/Decoders/ThumbnailPipeline.cpp"
rg -q 'CreateSvgDocument|DrawSvgDocument' "$project_root/Decoders/SvgRenderer.cpp"
rg -q 'TypeOverlay' "$project_root/Installer/Setup.cpp"
rg -q 'InstalledOverlayPresent' "$project_root/Installer/Setup.cpp"
rg -q 'AssocQueryStringW\(ASSOCF_NONE, ASSOCSTR_DEFAULTICON' "$project_root/Installer/Setup.cpp"
rg -q 'SetString\(HKEY_CURRENT_USER, target, L"TypeOverlay", icon\)' "$project_root/Installer/Setup.cpp"
rg -q 'CreateUpdaterShortcut' "$project_root/Installer/Setup.cpp"
rg -q 'ArtThumbUpdate.exe' "$project_root/Installer/Setup.cpp"
rg -q 'ArtThumb Settings.lnk' "$project_root/Installer/Setup.cpp"
rg -q 'SettingsPath' "$project_root/Installer/Setup.cpp"
rg -Fq 'Software\\ArtThumb\\Settings' "$project_root/Installer/Setup.cpp"
rg -q 'kGitHubLatestReleaseApiPath' "$project_root/Provider/Module.h"
rg -q 'WinHttpOpen' "$project_root/Updater/Main.cpp"
rg -q 'WinHttpOpenRequest\(connection' "$project_root/Updater/Main.cpp"
rg -q 'PostMessageW\(window, kUpdateComplete' "$project_root/Updater/Main.cpp"
rg -q 'UpdaterPath' "$project_root/Tests/VerifyInstallation.ps1"
rg -q 'ArtThumb Update.lnk' "$project_root/Tests/VerifyInstallation.ps1"
rg -q 'WICBitmapInterpolationModeHighQualityCubic' "$project_root/Decoders/WicImage.cpp"
rg -q 'FindXmpImage\(bytes, encoded\) \|\| FindJpeg' "$project_root/Decoders/EmbeddedPreview.cpp"
rg -q 'entity == "&#xA;"' "$project_root/Decoders/EmbeddedPreview.cpp"
indd_decoder="$(sed -n '/HRESULT DecodeInddPreview/,/^}/p' \
  "$project_root/Decoders/EmbeddedPreview.cpp")"
if rg -q 'FindJpeg' <<<"$indd_decoder"; then
  echo "INDD must not use arbitrary embedded JPEG assets as its document preview." >&2
  exit 1
fi

if rg -q 'DisableProcessIsolation' "$project_root/Provider" "$project_root/Decoders" \
    "$project_root/Compat" "$project_root/Installer" "$project_root/resources"; then
  echo "Process isolation must not be disabled." >&2
  exit 1
fi
if rg -q 'ApplyThumbnailSharpness|SharpnessSlider|SettingsApp/Main.cpp' \
    "$project_root/Provider" "$project_root/Decoders" "$project_root/CMakeLists.txt" \
    "$project_root/scripts/build-cross.sh"; then
  echo "Thumbnail preview and sharpness settings must not be part of the app or provider." >&2
  exit 1
fi

echo "ArtThumb provider, installer, update checker, resources, runtime dependencies, and registrations are valid."
