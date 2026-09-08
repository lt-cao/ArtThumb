cmake_minimum_required(VERSION 3.24)
project(ArtThumb VERSION 1.2.1 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(ArtThumbProvider SHARED
    Provider/DllMain.cpp
    Provider/ArtThumbProvider.def
    Provider/Module.cpp
    Provider/ThumbnailProvider.cpp
    Provider/ClassFactory.cpp
    Provider/Registration.cpp
    Decoders/StreamReader.cpp
    Decoders/WicImage.cpp
    Decoders/ThumbnailEffects.cpp
    Decoders/EmbeddedPreview.cpp
    Decoders/PsdDecoder.cpp
    Decoders/PdfRenderer.cpp
    Decoders/ThumbnailPipeline.cpp
    Settings/UserSettings.cpp
    resources/provider.rc
)

target_include_directories(ArtThumbProvider PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(ArtThumbProvider PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000000)
target_compile_options(ArtThumbProvider PRIVATE -Wall -Wextra -Wpedantic -Wno-unknown-pragmas)
target_link_libraries(ArtThumbProvider PRIVATE
    windowscodecs shcore shlwapi ole32 runtimeobject gdi32 advapi32 shell32
)
set_target_properties(ArtThumbProvider PROPERTIES
    PREFIX ""
    OUTPUT_NAME "ArtThumbProvider"
    SUFFIX ".dll"
)

add_executable(ArtThumbSetup WIN32
    Installer/Setup.cpp
    resources/setup.rc
)
target_include_directories(ArtThumbSetup PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/resources
)
target_compile_definitions(ArtThumbSetup PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000000)
target_compile_options(ArtThumbSetup PRIVATE -Wall -Wextra -Wpedantic -Wno-unknown-pragmas)
target_link_libraries(ArtThumbSetup PRIVATE advapi32 ole32 shell32 shlwapi user32)
set_target_properties(ArtThumbSetup PROPERTIES SUFFIX ".exe")

add_executable(ArtThumbSettings WIN32
    SettingsApp/Main.cpp
    Settings/UserSettings.cpp
    Decoders/ThumbnailEffects.cpp
    resources/settings.rc
)
target_include_directories(ArtThumbSettings PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(ArtThumbSettings PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000000)
target_compile_options(ArtThumbSettings PRIVATE -Wall -Wextra -Wpedantic -Wno-unknown-pragmas)
target_link_libraries(ArtThumbSettings PRIVATE
    advapi32 comctl32 gdi32 shell32 shlwapi user32 winhttp
)
set_target_properties(ArtThumbSettings PROPERTIES SUFFIX ".exe")

install(TARGETS ArtThumbProvider ArtThumbSetup ArtThumbSettings
        RUNTIME DESTINATION . LIBRARY DESTINATION .)
