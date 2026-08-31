FetchContent_Declare(entt_3_16
        GIT_REPOSITORY https://github.com/skypjack/entt.git
        GIT_TAG 1333fa53129e7cfded5a9640c4336a254049917b
        SOURCE_SUBDIR do-not-add-subdirectory
        EXCLUDE_FROM_ALL)

FetchContent_Declare(entt_4_0
        GIT_REPOSITORY https://github.com/skypjack/entt.git
        GIT_TAG v4.0.0
        SOURCE_SUBDIR do-not-add-subdirectory
        EXCLUDE_FROM_ALL)

FetchContent_MakeAvailable(entt_3_16 entt_4_0)

add_library(entt_3_16 INTERFACE)
add_library(EnTT::v3_16 ALIAS entt_3_16)
target_include_directories(entt_3_16 INTERFACE ${entt_3_16_SOURCE_DIR}/src)

add_library(entt_4_0 INTERFACE)
add_library(EnTT::v4_0 ALIAS entt_4_0)
target_include_directories(entt_4_0 INTERFACE ${entt_4_0_SOURCE_DIR}/src)

function(entt_for_client version out)
    if (version VERSION_LESS 1.26.40)
        message(FATAL_ERROR "1.26.40 is the oldest client spyglass carries patterns for, got ${version}")
    elseif (version VERSION_LESS 1.26.50)
        set(${out} EnTT::v3_16 PARENT_SCOPE)
    else ()
        set(${out} EnTT::v4_0 PARENT_SCOPE)
    endif ()
endfunction()
