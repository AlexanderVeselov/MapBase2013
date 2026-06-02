source_sdk_shared_library(serverplugin_empty
    serverplugin_bot.cpp
    serverplugin_empty.cpp
)

target_include_directories(serverplugin_empty PRIVATE
    "${SOURCE_SDK_ROOT}/game/server"
    "${SOURCE_SDK_ROOT}/game/shared"
)

target_compile_definitions(serverplugin_empty PRIVATE
    serverplugin_emptyONLY
    _MBCS
)

source_sdk_imported_library(tier2)

target_link_libraries(serverplugin_empty PRIVATE
    mathlib
    tier2
    odbc32.lib
    odbccp32.lib
)