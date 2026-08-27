cmake_minimum_required(VERSION 3.23)
project(spyglass VERSION 0.1.0 LANGUAGES CXX)

if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(SPYGLASS_PLATFORM windows)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
elseif (ANDROID AND ANDROID_ABI STREQUAL "x86_64")
    set(SPYGLASS_PLATFORM android)
else ()
    message(FATAL_ERROR "spyglass targets 64-bit Windows, or Android x86_64 for the Linux launcher")
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SPYGLASS_SOURCES)

if (SPYGLASS_PLATFORM STREQUAL "windows")
    list(APPEND SPYGLASS_SOURCES src/spyglass/dllmain.cpp)
else ()
    list(APPEND SPYGLASS_SOURCES src/spyglass/modmain.cpp)
endif ()

add_library(spyglass SHARED ${SPYGLASS_SOURCES})
target_include_directories(spyglass PRIVATE src)

if (SPYGLASS_PLATFORM STREQUAL "windows")
    add_executable(spyglass_inject src/injector/main.cpp)
    target_include_directories(spyglass_inject PRIVATE src)
    set_target_properties(spyglass_inject PROPERTIES OUTPUT_NAME spyglass)

    install(TARGETS spyglass spyglass_inject RUNTIME DESTINATION .)
else ()
    install(TARGETS spyglass LIBRARY DESTINATION .)
endif ()
