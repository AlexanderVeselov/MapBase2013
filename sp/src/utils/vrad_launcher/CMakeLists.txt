source_sdk_console_executable(vrad NO_MEMOVERRIDE
    vrad_launcher.cpp
    stdafx.cpp
)

source_sdk_msvc_precompiled_header(vrad
    stdafx.h
    "${CMAKE_CURRENT_SOURCE_DIR}/stdafx.cpp"
    vrad_launcher
)

target_include_directories(vrad PRIVATE
    "${SOURCE_SDK_ROOT}/public/tier1"
)