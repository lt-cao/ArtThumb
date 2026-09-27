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
settings="$project_root/build-cross/ArtThumbSettings.exe"
[[ -f "$dll" && -f "$setup" && -f "$settings" ]]

dll_report="$($reader --file-headers --coff-exports --coff-imports "$dll")"
setup_report="$($reader --file-headers --coff-resources "$setup")"
settings_report="$($reader --file-headers --coff-imports --coff-resources "$settings")"

rg -q 'Arch: x86_64' <<<"$dll_report"
rg -q 'IMAGE_FILE_DLL' <<<"$dll_report"
for export_name in DllCanUnloadNow DllGetClassObject DllRegisterServer DllUnregisterServer; do
  rg -q "Name: $export_name" <<<"$dll_report"
done
rg -q 'Type: MANIFEST' <<<"$setup_report"
rg -q 'Type: VERSIONINFO' <<<"$setup_report"
rg -q 'Arch: x86_64' <<<"$settings_report"
rg -q 'IMAGE_SUBSYSTEM_WINDOWS_GUI' <<<"$settings_report"
rg -q 'Type: MANIFEST' <<<"$settings_report"
rg -q 'Type: VERSIONINFO' <<<"$settings_report"
rg -q 'Name: WINHTTP.dll' <<<"$settings_report"

if rg -q 'Name: (libstdc\+\+|libgcc|libwinpthread|libc\+\+).*\.dll' \
    <<<"$dll_report$settings_report"; then
  echo "Unexpected compiler runtime DLL dependency." >&2
  exit 1
fi

for extension in psd psb ai eps indd pdf; do
  rg -q "L\"\\.$extension\"" "$project_root/Provider/Registration.cpp"
  rg -q "L\"\\.$extension\"" "$project_root/Installer/Setup.cpp"
done
rg -q 'ApplyThumbnailSharpness' "$project_root/Provider/ThumbnailProvider.cpp"
rg -q 'LoadUserSettings' "$project_root/Provider/ThumbnailProvider.cpp"
rg -q 'TypeOverlay' "$project_root/Installer/Setup.cpp"
rg -q 'InstalledOverlayPresent' "$project_root/Installer/Setup.cpp"
rg -q 'return DeleteNamedValue\(HKEY_CURRENT_USER, target, L"TypeOverlay"\)' "$project_root/Installer/Setup.cpp"
rg -q 'kGitHubLatestReleaseApiPath' "$project_root/SettingsApp/Main.cpp"
rg -q 'WinHttpOpen' "$project_root/SettingsApp/Main.cpp"
rg -q 'ArtThumbSettings.exe' "$project_root/Installer/Setup.cpp"
if rg -q 'AddApplicationBadge|ApplicationBadge\.cpp|badgePercent|BadgePercent' \
    "$project_root/Provider" "$project_root/Settings" "$project_root/SettingsApp" \
    "$project_root/CMakeLists.txt" "$project_root/scripts/build-cross.sh"; then
  echo "Thumbnail pixels and settings must not contain application badges." >&2
  exit 1
fi
rg -q 'DecodeComposite\(reader, info, edge, bitmap\)' "$project_root/Decoders/PsdDecoder.cpp"
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

echo "ArtThumb provider, installer, settings UI, resources, runtime dependencies, and registrations are valid."
