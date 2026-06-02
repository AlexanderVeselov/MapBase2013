source_sdk_console_executable(captioncompiler
    "${SOURCE_SDK_ROOT}/public/tier0/memoverride.cpp"
    captioncompiler.cpp
    "${SOURCE_SDK_ROOT}/common/compiledcaptionswap.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/filesystem_tools.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/cmdlib.cpp"
    "${SOURCE_SDK_ROOT}/public/filesystem_helpers.cpp"
    "${SOURCE_SDK_ROOT}/public/filesystem_init.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/pacifier.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/scriplib.cpp"
    "${SOURCE_SDK_ROOT}/public/stringregistry.cpp"
)

target_include_directories(captioncompiler PRIVATE
    "${SOURCE_SDK_ROOT}/utils/common"
    "${SOURCE_SDK_ROOT}/game/shared"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_definitions(captioncompiler PRIVATE
    PROTECTED_THINGS_DISABLE
    captioncompiler
)

source_sdk_imported_library(appframework)
source_sdk_imported_library(tier2)
source_sdk_imported_library(tier3)

target_link_libraries(captioncompiler PRIVATE
    appframework
    mathlib
    tier2
    tier3
)