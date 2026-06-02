source_sdk_console_executable(motionmapper
    "${SOURCE_SDK_ROOT}/public/tier0/memoverride.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/cmdlib.cpp"
    "${SOURCE_SDK_ROOT}/public/filesystem_helpers.cpp"
    "${SOURCE_SDK_ROOT}/public/filesystem_init.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/filesystem_tools.cpp"
    motionmapper.cpp
    "${SOURCE_SDK_ROOT}/utils/common/scriplib.cpp"
)

target_include_directories(motionmapper PRIVATE
    "${SOURCE_SDK_ROOT}/utils/common"
    "${SOURCE_SDK_ROOT}/utils/nvtristriplib"
    "${SOURCE_SDK_ROOT}/Game_Shared"
)

target_compile_definitions(motionmapper PRIVATE
    PROTECTED_THINGS_DISABLE
)

source_sdk_imported_library(nvtristrip)
source_sdk_imported_library(tier2)

target_link_libraries(motionmapper PRIVATE
    mathlib
    nvtristrip
    tier2
    winmm.lib
)