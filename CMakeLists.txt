cmake_minimum_required(VERSION 3.21)
project(TPMate LANGUAGES C CXX)

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
    src/main.cpp
    src/StartupRegistration.cpp
    src/TrayApp.cpp
    src/IRacingMonitor.cpp
    src/SessionInfo.cpp
    src/TradingPaintsClient.cpp
    src/PaintStore.cpp
    src/PaintDownloader.cpp
)

target_include_directories(TPMate PRIVATE src)
target_compile_definitions(TPMate PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(TPMate PRIVATE shell32 winhttp xmllite ole32 oleaut32 advapi32 taskschd bzip2)

if(MSVC)
    set_property(TARGET bzip2 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET TPMate PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(TPMate PRIVATE $<$<CONFIG:Release>:/O2 /Oi /Gy /GF /Gw>)
    target_link_options(TPMate PRIVATE $<$<CONFIG:Release>:/OPT:REF /OPT:ICF /INCREMENTAL:NO>)
endif()
