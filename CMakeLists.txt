cmake_minimum_required(VERSION 3.21)
project(TPMate LANGUAGES C CXX RC)

# The one place for the version. The release workflow checks that a "vX.Y.Z" tag matches
# it and passes the tag's version in as TPMATE_VERSION.
set(TPMATE_VERSION_DEFAULT "0.1.0")
set(TPMATE_VERSION "${TPMATE_VERSION_DEFAULT}" CACHE STRING "TPMate version")
if(NOT TPMATE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+([-.+][0-9A-Za-z.-]+)?$")
    message(FATAL_ERROR "Invalid TPMATE_VERSION: '${TPMATE_VERSION}'")
endif()
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" TPMATE_VERSION_NUMBERS "${TPMATE_VERSION}")
set(TPMATE_VERSION_MAJOR ${CMAKE_MATCH_1})
set(TPMATE_VERSION_MINOR ${CMAKE_MATCH_2})
set(TPMATE_VERSION_PATCH ${CMAKE_MATCH_3})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(bzip2 STATIC
    third_party/bzip2/blocksort.c
    third_party/bzip2/bzlib.c
    third_party/bzip2/compress.c
    third_party/bzip2/crctable.c
    third_party/bzip2/decompress.c
    third_party/bzip2/huffman.c
    third_party/bzip2/randtable.c
)
target_include_directories(bzip2 PUBLIC third_party/bzip2)

add_executable(TPMate WIN32
    resources/resources.rc
    src/main.cpp
    src/JsonLite.cpp
    src/UpdateChecker.cpp
    src/StartupRegistration.cpp
    src/TrayApp.cpp
    src/AppSettings.cpp
    src/SessionPage.cpp
    src/PaintsPage.cpp
    src/SettingsPage.cpp
    src/IRacingMonitor.cpp
    src/CarPresence.cpp
    src/SessionInfo.cpp
    src/TradingPaintsClient.cpp
    src/PaintStore.cpp
    src/PaintDownloader.cpp
    src/DriverStatus.cpp
)

target_include_directories(TPMate PRIVATE src)
target_compile_definitions(TPMate PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
# Also reach resources/resources.rc, which builds VERSIONINFO from them.
target_compile_definitions(TPMate PRIVATE TPMATE_VERSION="${TPMATE_VERSION}"
    TPMATE_VERSION_MAJOR=${TPMATE_VERSION_MAJOR} TPMATE_VERSION_MINOR=${TPMATE_VERSION_MINOR}
    TPMATE_VERSION_PATCH=${TPMATE_VERSION_PATCH})
target_link_libraries(TPMate PRIVATE comctl32 uxtheme gdi32 shell32 winhttp bcrypt xmllite ole32 oleaut32 advapi32 taskschd bzip2)

add_executable(TPMateUpdateTests EXCLUDE_FROM_ALL tests/UpdateCheckerTests.cpp src/JsonLite.cpp)
add_executable(TPMateReloadTests EXCLUDE_FROM_ALL tests/ReloadShortcutTests.cpp)
add_executable(TPMateDriverStatusTests EXCLUDE_FROM_ALL tests/DriverStatusTests.cpp src/DriverStatus.cpp src/CarPresence.cpp src/SessionInfo.cpp)
target_include_directories(TPMateDriverStatusTests PRIVATE src)
target_compile_definitions(TPMateDriverStatusTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
add_executable(TPMateDownloaderTests EXCLUDE_FROM_ALL tests/DownloaderTests.cpp src/PaintDownloader.cpp src/DriverStatus.cpp src/CarPresence.cpp src/SessionInfo.cpp)
target_include_directories(TPMateDownloaderTests PRIVATE src)
target_compile_definitions(TPMateDownloaderTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(TPMateDownloaderTests PRIVATE ole32)
add_executable(TPMatePaintStoreTests EXCLUDE_FROM_ALL tests/PaintStoreTests.cpp src/PaintStore.cpp)
target_include_directories(TPMatePaintStoreTests PRIVATE src)
target_compile_definitions(TPMatePaintStoreTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(TPMatePaintStoreTests PRIVATE shell32 bzip2)
add_executable(TPMateTradingPaintsTests EXCLUDE_FROM_ALL tests/TradingPaintsTests.cpp)
target_include_directories(TPMateTradingPaintsTests PRIVATE src)
target_compile_definitions(TPMateTradingPaintsTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(TPMateTradingPaintsTests PRIVATE winhttp xmllite ole32)
add_executable(TPMatePresenceTests EXCLUDE_FROM_ALL tests/CarPresenceTests.cpp src/CarPresence.cpp src/SessionInfo.cpp)
target_include_directories(TPMatePresenceTests PRIVATE src)
target_compile_definitions(TPMatePresenceTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(TPMateReloadTests PRIVATE src)
target_compile_definitions(TPMateReloadTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(TPMateUpdateTests PRIVATE src)
# The update tests compare against fixed release tags, so they always run as version 0.1.0,
# whatever version a release build passes in.
target_compile_definitions(TPMateUpdateTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN TPMATE_VERSION="0.1.0")
target_link_libraries(TPMateUpdateTests PRIVATE shell32 winhttp bcrypt)

if(MSVC)
    # Sources stay ASCII (escapes for other characters); /utf-8 avoids the system code page.
    target_compile_options(TPMate PRIVATE /EHsc /utf-8)
    target_compile_options(TPMateUpdateTests PRIVATE /EHsc)
    target_compile_options(TPMateReloadTests PRIVATE /EHsc)
    target_compile_options(TPMatePresenceTests PRIVATE /EHsc)
    target_compile_options(TPMateDriverStatusTests PRIVATE /EHsc)
    foreach(test TPMateDownloaderTests TPMatePaintStoreTests TPMateTradingPaintsTests)
        target_compile_options(${test} PRIVATE /EHsc)
        set_property(TARGET ${test} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endforeach()
    set_property(TARGET TPMateDriverStatusTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET TPMatePresenceTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET TPMateReloadTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET TPMateUpdateTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_options(TPMate PRIVATE "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'")
    set_property(TARGET bzip2 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET TPMate PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(TPMate PRIVATE $<$<CONFIG:Release>:/O2 /Oi /Gy /GF /Gw>)
    target_link_options(TPMate PRIVATE $<$<CONFIG:Release>:/OPT:REF /OPT:ICF /INCREMENTAL:NO>)
endif()
