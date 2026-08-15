cmake_minimum_required(VERSION 3.23)
project(spyglass VERSION 0.1.0 LANGUAGES CXX)

if (NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "spyglass targets 64-bit Windows only")
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

file(GLOB_RECURSE RUNTIME_SOURCES CONFIGURE_DEPENDS src/spyglass/*.cpp)
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
