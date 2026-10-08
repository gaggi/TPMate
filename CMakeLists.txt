cmake_minimum_required(VERSION 3.21)
project(TPMate VERSION 0.1.0 LANGUAGES C CXX RC)

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
    src/PaintsPage.cpp
    src/SettingsPage.cpp
    src/IRacingMonitor.cpp
    src/CarPresence.cpp
    src/SessionInfo.cpp
    src/TradingPaintsClient.cpp
    src/PaintStore.cpp
    src/PaintDownloader.cpp
)

target_include_directories(TPMate PRIVATE src)
target_compile_definitions(TPMate PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_definitions(TPMate PRIVATE TPMATE_VERSION="${PROJECT_VERSION}")
target_link_libraries(TPMate PRIVATE comctl32 uxtheme gdi32 shell32 winhttp xmllite ole32 oleaut32 advapi32 taskschd bzip2)

add_executable(TPMateUpdateTests EXCLUDE_FROM_ALL tests/UpdateCheckerTests.cpp src/JsonLite.cpp)
add_executable(TPMateReloadTests EXCLUDE_FROM_ALL tests/ReloadShortcutTests.cpp)
add_executable(TPMatePresenceTests EXCLUDE_FROM_ALL tests/CarPresenceTests.cpp src/CarPresence.cpp src/SessionInfo.cpp)
target_include_directories(TPMatePresenceTests PRIVATE src)
target_compile_definitions(TPMatePresenceTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(TPMateReloadTests PRIVATE src)
target_compile_definitions(TPMateReloadTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(TPMateUpdateTests PRIVATE src)
target_compile_definitions(TPMateUpdateTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN TPMATE_VERSION="${PROJECT_VERSION}")
target_link_libraries(TPMateUpdateTests PRIVATE shell32 winhttp)

if(MSVC)
    # Sources stay ASCII (escapes for other characters); /utf-8 avoids the system code page.
    target_compile_options(TPMate PRIVATE /EHsc /utf-8)
    target_compile_options(TPMateUpdateTests PRIVATE /EHsc)
    target_compile_options(TPMateReloadTests PRIVATE /EHsc)
    target_compile_options(TPMatePresenceTests PRIVATE /EHsc)
    set_property(TARGET TPMatePresenceTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET TPMateReloadTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET TPMateUpdateTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_options(TPMate PRIVATE "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'")
    set_property(TARGET bzip2 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET TPMate PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(TPMate PRIVATE $<$<CONFIG:Release>:/O2 /Oi /Gy /GF /Gw>)
    target_link_options(TPMate PRIVATE $<$<CONFIG:Release>:/OPT:REF /OPT:ICF /INCREMENTAL:NO>)
endif()
