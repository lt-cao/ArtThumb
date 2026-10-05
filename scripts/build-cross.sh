#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "$0")/.." && pwd)"
toolchain_root="${LLVM_MINGW_ROOT:-}"
if [[ -z "$toolchain_root" ]]; then
  echo "Set LLVM_MINGW_ROOT to the extracted llvm-mingw directory." >&2
  exit 2
fi

tool_bin="$toolchain_root/bin"
build_dir="$project_root/build-cross"
mkdir -p "$build_dir"

common_flags=(
  -std=c++20 -O2 -DNDEBUG -static
  -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX
  -D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A000003
  -Wall -Wextra -Wpedantic -Wno-unknown-pragmas
  -I"$project_root"
)

(
  cd "$project_root/resources"
  "$tool_bin/x86_64-w64-mingw32-windres" provider.rc -I. -O coff \
    -o "$build_dir/provider.res"
  "$tool_bin/x86_64-w64-mingw32-windres" setup.rc -I. -O coff \
    -o "$build_dir/setup.res"
  "$tool_bin/x86_64-w64-mingw32-windres" updater.rc -I. -O coff \
    -o "$build_dir/updater.res"
)

provider_sources=(
  Provider/DllMain.cpp
  Provider/Module.cpp
  Provider/ThumbnailProvider.cpp
  Provider/ClassFactory.cpp
  Provider/Registration.cpp
  Decoders/StreamReader.cpp
  Decoders/WicImage.cpp
  Decoders/EmbeddedPreview.cpp
  Decoders/PsdDecoder.cpp
  Decoders/PdfRenderer.cpp
  Decoders/SvgRenderer.cpp
  Decoders/ThumbnailPipeline.cpp
)
provider_paths=()
for source in "${provider_sources[@]}"; do
  provider_paths+=("$project_root/$source")
done

"$tool_bin/x86_64-w64-mingw32-clang++" \
  "${common_flags[@]}" -shared -Wl,--no-undefined \
  "${provider_paths[@]}" "$project_root/Provider/ArtThumbProvider.def" "$build_dir/provider.res" \
  -ladvapi32 -lgdi32 -lole32 -lruntimeobject -lshell32 -lshcore \
  -lshlwapi -luuid -lwindowscodecs -ld2d1 -ld3d11 -ldxgi \
  -o "$build_dir/ArtThumbProvider.dll"

"$tool_bin/x86_64-w64-mingw32-clang++" \
  "${common_flags[@]}" -municode -mwindows \
  "$project_root/Installer/Setup.cpp" "$build_dir/setup.res" \
  -ladvapi32 -lole32 -lshell32 -lshlwapi -luser32 -luuid \
  -o "$build_dir/ArtThumbSetup.exe"

"$tool_bin/x86_64-w64-mingw32-clang++" \
  "${common_flags[@]}" -municode -mwindows \
  "$project_root/Updater/Main.cpp" "$build_dir/updater.res" \
  -lshell32 -luser32 -lwinhttp \
  -o "$build_dir/ArtThumbUpdate.exe"

echo "Release files:"
echo "  $build_dir/ArtThumbProvider.dll"
echo "  $build_dir/ArtThumbSetup.exe"
echo "  $build_dir/ArtThumbUpdate.exe"
