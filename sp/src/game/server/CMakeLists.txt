if(SOURCE_SDK_GAME STREQUAL "episodic")
    include(server_episodic_sources.cmake)
    include(server_episodic_pch.cmake)
    set(SERVER_GAME_SOURCES ${SERVER_EPISODIC_SOURCES})
    set(SERVER_NO_PCH_SOURCES ${SERVER_EPISODIC_NO_PCH_SOURCES})
    set(SERVER_GAME_CAPS EPISODIC)
    set(SERVER_VPC_GAME episodic)
    set(SERVER_PROTO_SUFFIX episodic)
    set(SERVER_IS_EPISODIC TRUE)
elseif(SOURCE_SDK_GAME STREQUAL "hl2")
    include(server_hl2_sources.cmake)
    include(server_hl2_pch.cmake)
    set(SERVER_GAME_SOURCES ${SERVER_HL2_SOURCES})
    set(SERVER_NO_PCH_SOURCES ${SERVER_HL2_NO_PCH_SOURCES})
    set(SERVER_GAME_CAPS VALVE)
    set(SERVER_VPC_GAME valve)
    set(SERVER_PROTO_SUFFIX hl2)
    set(SERVER_IS_EPISODIC FALSE)
else()
    message(FATAL_ERROR "Unsupported SOURCE_SDK_GAME=${SOURCE_SDK_GAME}. Expected episodic or hl2.")
endif()

source_sdk_shared_library(server
    ${SERVER_GAME_SOURCES}
)

source_sdk_copy_game_dll(server)

source_sdk_msvc_precompiled_header(server
    cbase.h
    "${CMAKE_CURRENT_SOURCE_DIR}/stdafx.cpp"
    server
    ${SERVER_NO_PCH_SOURCES}
)

target_include_directories(server PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SOURCE_SDK_ROOT}/game/shared"
    "${SOURCE_SDK_ROOT}/utils/common"
    "${SOURCE_SDK_ROOT}/game/shared/econ"
    "${SOURCE_SDK_ROOT}/game/server/NextBot"
    "${SOURCE_SDK_ROOT}/game/shared/hl2"
    "${CMAKE_CURRENT_SOURCE_DIR}/hl2"
    "${SOURCE_SDK_ROOT}/game/server/generated_proto_mod_${SERVER_PROTO_SUFFIX}"
    "${SOURCE_SDK_ROOT}/thirdparty/protobuf-2.3.0/src"
)

if(SERVER_IS_EPISODIC)
    target_include_directories(server PRIVATE
        "${SOURCE_SDK_ROOT}/game/shared/episodic"
        "${CMAKE_CURRENT_SOURCE_DIR}/episodic"
    )
endif()

target_compile_definitions(server PRIVATE
    GAME_DLL
    VECTOR
    VERSION_SAFE_STEAM_API_INTERFACES
    PROTECTED_THINGS_ENABLE
    sprintf=use_Q_snprintf_instead_of_sprintf
    strncpy=use_Q_strncpy_instead
    _snprintf=use_Q_snprintf_instead
    fopen=dont_use_fopen
    HL2_DLL
    USES_SAVERESTORE
    _EXTERNAL_DLL_EXT=.dll
    VPCGAMECAPS=${SERVER_GAME_CAPS}
    SOURCE1=1
    VPCGAME=${SERVER_VPC_GAME}
    ASW_PROJECTED_TEXTURES
    DYNAMIC_RTT_SHADOWS
    GLOWS_ENABLE
)

if(SERVER_IS_EPISODIC)
    target_compile_definitions(server PRIVATE HL2_EPISODIC)
endif()

source_sdk_imported_library(choreoobjects)
source_sdk_imported_library(dmxloader)
source_sdk_imported_library(particles)
source_sdk_imported_library(tier2)
source_sdk_imported_library(tier3)
source_sdk_imported_library(steam_api)
source_sdk_imported_library(libprotobuf)

target_link_libraries(server PRIVATE
    choreoobjects
    dmxloader
    mathlib
    particles
    tier2
    tier3
    steam_api
    libprotobuf
    vscript
    responserules
)
