source_sdk_static_library(raytrace
    raytrace.cpp
    trace2.cpp
    trace3.cpp
)

target_include_directories(raytrace PRIVATE
    "${SOURCE_SDK_ROOT}/utils/common"
)
