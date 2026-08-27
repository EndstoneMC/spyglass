FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG a0bfbe4d8f6ddf7f678e6aeac7b1253fe2fc9cda
        EXCLUDE_FROM_ALL)

FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})

if (WIN32)
    target_sources(imgui PRIVATE
            ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp)
    target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(imgui PUBLIC d3d11 d3d12 dxgi dxguid)
endif ()
