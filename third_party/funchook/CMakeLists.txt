set(FUNCHOOK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FUNCHOOK_BUILD_SHARED OFF CACHE BOOL "" FORCE)

FetchContent_Declare(funchook
        GIT_REPOSITORY https://github.com/kubo/funchook.git
        GIT_TAG v1.1.3
        GIT_SHALLOW TRUE
        GIT_SUBMODULES_RECURSE TRUE)

FetchContent_MakeAvailable(funchook)
