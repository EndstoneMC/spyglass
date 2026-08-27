cmake_minimum_required(VERSION 3.28)
project(spyglass VERSION 0.1.0 LANGUAGES CXX)

if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
elseif (NOT (ANDROID AND ANDROID_ABI STREQUAL "x86_64"))
    message(FATAL_ERROR "spyglass targets 64-bit Windows, or Android x86_64 for the Linux launcher")
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(third_party)

set(SPYGLASS_SOURCES
        src/spyglass/core/log.cpp
        src/spyglass/core/output.cpp
        src/spyglass/core/time.cpp
        src/spyglass/hook/function_hook.cpp
        src/spyglass/main.cpp
        src/spyglass/overlay/view.cpp)

if (WIN32)
    list(APPEND SPYGLASS_SOURCES
            src/spyglass/overlay/d3d11_backend.cpp
            src/spyglass/overlay/d3d12_backend.cpp
            src/spyglass/overlay/input.cpp
            src/spyglass/overlay/overlay.cpp)
else ()
    list(APPEND SPYGLASS_SOURCES
            src/spyglass/hook/host_symbol.cpp
            src/spyglass/overlay/launcher_overlay.cpp)
endif ()

add_library(spyglass SHARED ${SPYGLASS_SOURCES})
target_include_directories(spyglass PRIVATE src)
target_link_libraries(spyglass PRIVATE
        nonstd::expected-lite
        funchook-static
        imgui
        libhat::libhat)

if (ANDROID)
    target_link_libraries(spyglass PRIVATE log)
endif ()

install(TARGETS spyglass RUNTIME DESTINATION . LIBRARY DESTINATION .)

if (WIN32)
    install(FILES $<TARGET_PDB_FILE:spyglass> DESTINATION . OPTIONAL)

    add_executable(spyglass_injector src/injector/main.cpp)
    target_include_directories(spyglass_injector PRIVATE src)
    set_target_properties(spyglass_injector PROPERTIES OUTPUT_NAME spyglass)

    install(TARGETS spyglass_injector RUNTIME DESTINATION .)
endif ()
