if(NOT MSVC)
    message(FATAL_ERROR "This initial CMake port currently supports MSVC only.")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(source_sdk_base INTERFACE)

target_include_directories(source_sdk_base INTERFACE
    "${SOURCE_SDK_ROOT}/common"
    "${SOURCE_SDK_ROOT}/public"
    "${SOURCE_SDK_ROOT}/public/tier0"
    "${SOURCE_SDK_ROOT}/public/tier1"
)

target_compile_definitions(source_sdk_base INTERFACE
    VPC
    DEV_BUILD
    FRAME_POINTER_OMISSION_DISABLED
    COMPILER_MSVC
    _DLL_EXT=.dll
    _CRT_SECURE_NO_DEPRECATE
    _CRT_NONSTDC_NO_DEPRECATE
    _ALLOW_RUNTIME_LIBRARY_MISMATCH
    _ALLOW_ITERATOR_DEBUG_LEVEL_MISMATCH
    _ALLOW_MSC_VER_MISMATCH
    $<$<CONFIG:Debug>:_HAS_ITERATOR_DEBUGGING=0>
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Debug>:_LIB>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
    $<$<NOT:$<CONFIG:Debug>>:_LIB>
    $<$<BOOL:${SOURCE_SDK_MAPBASE}>:MAPBASE>
    $<$<BOOL:${SOURCE_SDK_SOURCESDK}>:SOURCESDK>
    $<$<BOOL:${SOURCE_SDK_SOURCESDK}>:RAD_TELEMETRY_DISABLED>
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_compile_definitions(source_sdk_base INTERFACE
        PLATFORM_64BITS
        WIN64
        _WIN64
        COMPILER_MSVC64
    )
else()
    target_compile_definitions(source_sdk_base INTERFACE
        WIN32
        _WIN32
        COMPILER_MSVC32
    )
endif()

target_compile_options(source_sdk_base INTERFACE
    /MP
    /W4
    /fp:fast
    /GF
    /GR
    /Zc:wchar_t
    /Zc:forScope
    /FC
    /wd4316
    /wd4838
    /wd4456
    /wd4457
    /wd4458
    /wd4459
    $<$<CONFIG:Debug>:/Od>
    $<$<CONFIG:Debug>:/ZI>
    $<$<NOT:$<CONFIG:Debug>>:/O2>
    $<$<NOT:$<CONFIG:Debug>>:/Ob2>
    $<$<NOT:$<CONFIG:Debug>>:/Oi>
    $<$<NOT:$<CONFIG:Debug>>:/Ot>
    $<$<NOT:$<CONFIG:Debug>>:/Gy>
    $<$<NOT:$<CONFIG:Debug>>:/Z7>
    $<$<NOT:$<CONFIG:Debug>>:/d2Zi+>
    $<$<NOT:$<CONFIG:Debug>>:/Oy->
)

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_compile_options(source_sdk_base INTERFACE /arch:SSE2)
endif()
