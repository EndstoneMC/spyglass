cmake_minimum_required(VERSION 3.28)
project(spyglass VERSION 0.2.0 LANGUAGES CXX)

if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
elseif (NOT (ANDROID AND ANDROID_ABI STREQUAL "x86_64"))
    message(FATAL_ERROR "spyglass targets 64-bit Windows, or Android x86_64 for the Linux launcher")
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(third_party)

add_library(spyglass SHARED
        src/spyglass/error.cpp
        src/spyglass/filename.cpp
        src/spyglass/hook.cpp
        src/spyglass/install.cpp
        src/spyglass/main.cpp
        src/spyglass/network.cpp
        src/spyglass/overlay/android/overlay.cpp
        src/spyglass/overlay/android/symbol.cpp
        src/spyglass/overlay/capture.cpp
        src/spyglass/overlay/pane/packet_bytes.cpp
        src/spyglass/overlay/pane/packet_details.cpp
        src/spyglass/overlay/pane/packet_list.cpp
        src/spyglass/overlay/pane/status_bar.cpp
        src/spyglass/overlay/pane/toolbar.cpp
        src/spyglass/overlay/view.cpp
        src/spyglass/overlay/windows/d3d11_backend.cpp
        src/spyglass/overlay/windows/d3d12_backend.cpp
        src/spyglass/overlay/windows/input.cpp
        src/spyglass/overlay/windows/mouse.cpp
        src/spyglass/overlay/windows/overlay.cpp
        src/spyglass/pattern.cpp
        src/spyglass/signature.cpp)
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
install(FILES LICENSE README.md DESTINATION .)

if (WIN32)
    install(FILES $<TARGET_PDB_FILE:spyglass> DESTINATION . OPTIONAL)

    add_executable(spyglass_injector src/injector/main.cpp)
    target_link_libraries(spyglass_injector PRIVATE advapi32 shell32)
    target_link_options(spyglass_injector PRIVATE /DEBUG:NONE)
    set_target_properties(spyglass_injector PROPERTIES OUTPUT_NAME spyglass)

    install(TARGETS spyglass_injector RUNTIME DESTINATION .)
endif ()
