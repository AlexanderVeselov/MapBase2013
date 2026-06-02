source_sdk_console_executable(vvis
    "${SOURCE_SDK_ROOT}/public/tier0/memoverride.cpp"
    vvis_launcher.cpp
    StdAfx.cpp
)

source_sdk_msvc_precompiled_header(vvis
    StdAfx.h
    "${CMAKE_CURRENT_SOURCE_DIR}/StdAfx.cpp"
    vvis_launcher
    "${SOURCE_SDK_ROOT}/public/tier0/memoverride.cpp"
)

target_include_directories(vvis PRIVATE
    "${SOURCE_SDK_ROOT}/utils/common"
    "${SOURCE_SDK_ROOT}/public/tier1"
)