cmake_minimum_required(VERSION 3.23)
project(spyglass VERSION 0.1.0 LANGUAGES CXX)

# Windows builds an injected DLL. The Linux launcher runs the Android build of the client
# under its own linker, so there the payload is an Android shared object it loads as a mod.
if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(SPYGLASS_PLATFORM windows)
elseif (ANDROID AND ANDROID_ABI STREQUAL "x86_64")
    set(SPYGLASS_PLATFORM android)
else ()
    message(FATAL_ERROR "spyglass targets 64-bit Windows, or Android x86_64 for the Linux launcher")
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE RUNTIME_SOURCES CONFIGURE_DEPENDS src/spyglass/*.cpp)

if (SPYGLASS_PLATFORM STREQUAL "windows")
    list(REMOVE_ITEM RUNTIME_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/src/spyglass/modmain.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/spyglass/overlay/launcher_overlay.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/spyglass/hook/host_symbol.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/spyglass/hook/vtables.cpp)

    find_package(expected-lite REQUIRED)
    find_package(funchook REQUIRED)
    find_package(imgui REQUIRED)
    find_package(libhat REQUIRED)

    # CMakeDeps puts the package root in a per-config variable and never on
    # CMAKE_PREFIX_PATH, so the backend sources have to be pointed at directly.
    string(TOUPPER "${CMAKE_BUILD_TYPE}" SPYGLASS_CONFIG)
    find_path(IMGUI_BINDINGS_DIR
            NAMES imgui_impl_win32.cpp
            HINTS "${imgui_PACKAGE_FOLDER_${SPYGLASS_CONFIG}}"
            PATH_SUFFIXES res/bindings bindings
            REQUIRED)

    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

    add_library(imgui_backends STATIC
            ${IMGUI_BINDINGS_DIR}/imgui_impl_win32.cpp
            ${IMGUI_BINDINGS_DIR}/imgui_impl_dx11.cpp
            ${IMGUI_BINDINGS_DIR}/imgui_impl_dx12.cpp)
    target_include_directories(imgui_backends PUBLIC ${IMGUI_BINDINGS_DIR})
    target_link_libraries(imgui_backends PUBLIC imgui::imgui d3d11 d3d12 dxgi dxguid)

    add_library(spyglass SHARED ${RUNTIME_SOURCES})
    target_include_directories(spyglass PRIVATE src)
    target_link_libraries(spyglass PRIVATE
            nonstd::expected-lite
            funchook::funchook
            libhat::libhat
            imgui_backends)

    add_executable(spyglass_inject src/injector/main.cpp)
    target_link_libraries(spyglass_inject PRIVATE advapi32 shell32)
    target_link_options(spyglass_inject PRIVATE /DEBUG:NONE)
    set_target_properties(spyglass_inject PROPERTIES OUTPUT_NAME spyglass)

    install(TARGETS spyglass spyglass_inject RUNTIME DESTINATION .)
else ()
    # The view is backend independent and is kept. What goes is the Direct3D plumbing under
    # it, which the launcher's own ImGui takes the place of.
    list(REMOVE_ITEM RUNTIME_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/src/spyglass/dllmain.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/spyglass/overlay/overlay.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/spyglass/overlay/input.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/spyglass/overlay/d3d11_backend.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/spyglass/overlay/d3d12_backend.cpp)

    # Conan is set up for the Windows toolchain only, so the two dependencies the mod needs
    # are fetched directly.
    include(FetchContent)
    FetchContent_Declare(expected-lite
            GIT_REPOSITORY https://github.com/martinmoene/expected-lite.git
            GIT_TAG v0.9.0
            GIT_SHALLOW TRUE)
    set(FUNCHOOK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(FUNCHOOK_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(funchook
            GIT_REPOSITORY https://github.com/kubo/funchook.git
            GIT_TAG v1.1.3
            GIT_SHALLOW TRUE
            GIT_SUBMODULES_RECURSE TRUE)
    # Pinned to the commit the launcher's own submodule sits on. The two copies share one
    # context, so a different revision would mean a different ImGuiContext layout.
    FetchContent_Declare(imgui
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG a0bfbe4d8f6ddf7f678e6aeac7b1253fe2fc9cda)
    FetchContent_MakeAvailable(expected-lite funchook imgui)

    add_library(imgui STATIC
            ${imgui_SOURCE_DIR}/imgui.cpp
            ${imgui_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
    target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})

    add_library(spyglass SHARED ${RUNTIME_SOURCES})
    target_include_directories(spyglass PRIVATE src)
    target_link_libraries(spyglass PRIVATE nonstd::expected-lite funchook-static imgui log)

    install(TARGETS spyglass LIBRARY DESTINATION .)
endif ()
