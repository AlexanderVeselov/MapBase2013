if(SOURCE_SDK_GAME STREQUAL "episodic")
    include(client_episodic_sources.cmake)
    include(client_episodic_pch.cmake)
    set(CLIENT_GAME_SOURCES ${CLIENT_EPISODIC_SOURCES})
    set(CLIENT_NO_PCH_SOURCES ${CLIENT_EPISODIC_NO_PCH_SOURCES})
    set(CLIENT_GAME_CAPS EPISODIC)
    set(CLIENT_VPC_GAME episodic)
    set(CLIENT_PROTO_SUFFIX episodic)
    set(CLIENT_IS_EPISODIC TRUE)
elseif(SOURCE_SDK_GAME STREQUAL "hl2")
    include(client_hl2_sources.cmake)
    include(client_hl2_pch.cmake)
    set(CLIENT_GAME_SOURCES ${CLIENT_HL2_SOURCES})
    set(CLIENT_NO_PCH_SOURCES ${CLIENT_HL2_NO_PCH_SOURCES})
    set(CLIENT_GAME_CAPS VALVE)
    set(CLIENT_VPC_GAME valve)
    set(CLIENT_PROTO_SUFFIX hl2)
    set(CLIENT_IS_EPISODIC FALSE)
else()
    message(FATAL_ERROR "Unsupported SOURCE_SDK_GAME=${SOURCE_SDK_GAME}. Expected episodic or hl2.")
endif()

source_sdk_shared_library(client
    ${CLIENT_GAME_SOURCES}
)

source_sdk_copy_game_dll(client)

source_sdk_msvc_precompiled_header(client
    cbase.h
    "${CMAKE_CURRENT_SOURCE_DIR}/stdafx.cpp"
    client
    ${CLIENT_NO_PCH_SOURCES}
)

target_include_directories(client PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/hl2"
    "${CMAKE_CURRENT_SOURCE_DIR}/hl2/elements"
    "${SOURCE_SDK_ROOT}/game/shared/hl2"
    "${SOURCE_SDK_ROOT}/public"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SOURCE_SDK_ROOT}/game/client/generated_proto_mod_${CLIENT_PROTO_SUFFIX}"
    "${SOURCE_SDK_ROOT}/thirdparty/protobuf-2.3.0/src"
    "${SOURCE_SDK_ROOT}/vgui2/include"
    "${SOURCE_SDK_ROOT}/vgui2/controls"
    "${SOURCE_SDK_ROOT}/game/shared"
    "${CMAKE_CURRENT_SOURCE_DIR}/game_controls"
)

if(SOURCE_SDK_SIXENSE)
    target_include_directories(client PRIVATE
        "${SOURCE_SDK_ROOT}/thirdparty/sixensesdk/include"
    )
endif()

if(CLIENT_IS_EPISODIC)
    target_include_directories(client PRIVATE
        "${SOURCE_SDK_ROOT}/game/shared/episodic"
    )
endif()

target_compile_definitions(client PRIVATE
    NO_STRING_T
    CLIENT_DLL
    VECTOR
    VERSION_SAFE_STEAM_API_INTERFACES
    PROTECTED_THINGS_ENABLE
    strncpy=use_Q_strncpy_instead
    _snprintf=use_Q_snprintf_instead
    ENABLE_CHROMEHTMLWINDOW
    fopen=dont_use_fopen
    HL2_CLIENT_DLL
    _EXTERNAL_DLL_EXT=.dll
    VPCGAMECAPS=${CLIENT_GAME_CAPS}
    VPCGAME=${CLIENT_VPC_GAME}
    SOURCE1=1
    ASW_PROJECTED_TEXTURES
    DYNAMIC_RTT_SHADOWS
    GLOWS_ENABLE
)

if(SOURCE_SDK_MAPBASE_VSCRIPT)
    target_compile_definitions(client PRIVATE MAPBASE_VSCRIPT)
endif()

if(SOURCE_SDK_NEW_RESPONSE_SYSTEM)
    target_compile_definitions(client PRIVATE NEW_RESPONSE_SYSTEM)
endif()

if(SOURCE_SDK_MAPBASE_RPC)
    target_compile_definitions(client PRIVATE
        MAPBASE_RPC
        DISCORD_RPC
        STEAM_RPC
    )
endif()

if(CLIENT_IS_EPISODIC)
    target_compile_definitions(client PRIVATE HL2_EPISODIC)
endif()

source_sdk_imported_library(bitmap)
source_sdk_imported_library(choreoobjects)
source_sdk_imported_library(dmxloader)
source_sdk_imported_library(matsys_controls)
source_sdk_imported_library(particles)
source_sdk_imported_library(tier2)
source_sdk_imported_library(tier3)
source_sdk_imported_library(vtf)
source_sdk_imported_library(steam_api)
source_sdk_imported_library(libz)
source_sdk_imported_library(libprotobuf)
source_sdk_imported_common_library(libcurl)

if(SOURCE_SDK_MAPBASE_RPC)
    source_sdk_imported_library(discord-rpc)
endif()

target_link_libraries(client PRIVATE
    bitmap
    choreoobjects
    dmxloader
    mathlib
    raytrace
    matsys_controls
    particles
    tier2
    tier3
    vgui_controls
    vtf
    steam_api
    libcurl
    libz
    libprotobuf
)

if(SOURCE_SDK_MAPBASE_VSCRIPT)
    target_link_libraries(client PRIVATE vscript)
endif()

if(SOURCE_SDK_NEW_RESPONSE_SYSTEM)
    target_link_libraries(client PRIVATE responserules)
endif()

if(SOURCE_SDK_MAPBASE_RPC)
    target_link_libraries(client PRIVATE discord-rpc)
endif()

if(SOURCE_SDK_RENDER_NEW)
    target_compile_definitions(client PRIVATE SOURCE_SDK_RENDER_NEW)
    target_link_libraries(client PRIVATE
        d3d11.lib
        d3d12.lib
        dxgi.lib
        d3dcompiler.lib
    )
endif()
