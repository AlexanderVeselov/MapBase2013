source_sdk_console_executable(vtf2tga
    "${SOURCE_SDK_ROOT}/public/tier0/memoverride.cpp"
    vtf2tga.cpp
)

source_sdk_imported_library(bitmap)
source_sdk_imported_library(tier2)
source_sdk_imported_library(vtf)

target_link_libraries(vtf2tga PRIVATE
    bitmap
    mathlib
    tier2
    vtf
)
