source_sdk_console_executable(height2normal NO_MEMOVERRIDE
    height2normal.cpp
)

source_sdk_imported_library(bitmap)
source_sdk_imported_library(tier2)

target_include_directories(height2normal PRIVATE
    "${SOURCE_SDK_ROOT}/utils/common"
)

target_link_libraries(height2normal PRIVATE
    bitmap
    mathlib
    tier2
)
