source_sdk_static_library(responserules
    criteriaset.cpp
    response_system.cpp
    response_types.cpp
    response_types_internal.cpp
    rr_convars.cpp
    rr_response.cpp
    rr_speechconcept.cpp
    rrrlib.cpp
)

target_include_directories(responserules PRIVATE
    "${SOURCE_SDK_ROOT}/public/responserules"
)

target_compile_definitions(responserules PRIVATE
    RR_RUNTIME
)
