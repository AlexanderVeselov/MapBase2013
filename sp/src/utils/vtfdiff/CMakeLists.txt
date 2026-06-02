source_sdk_console_executable(vtfdiff
    "${SOURCE_SDK_ROOT}/public/tier0/memoverride.cpp"
    vtfdiff.cpp
)

source_sdk_imported_library(bitmap)
source_sdk_imported_library(tier2)
source_sdk_imported_library(vtf)

target_link_libraries(vtfdiff PRIVATE
    bitmap
    mathlib
    tier2
    vtf
)
