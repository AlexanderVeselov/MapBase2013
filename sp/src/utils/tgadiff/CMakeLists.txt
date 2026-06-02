source_sdk_console_executable(tgadiff
    "${SOURCE_SDK_ROOT}/public/tier0/memoverride.cpp"
    tgadiff.cpp
)

source_sdk_imported_library(bitmap)
source_sdk_imported_library(tier2)

target_link_libraries(tgadiff PRIVATE
    bitmap
    mathlib
    tier2
)
