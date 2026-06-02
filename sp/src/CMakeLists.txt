cmake_minimum_required(VERSION 3.21)

project(SourceSDK2013SP LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(SourceSDKOptions)
include(SourceSDKCompileOptions)
include(SourceSDKTargets)

add_subdirectory(raytrace)
add_subdirectory(fgdlib)
add_subdirectory(mathlib)
add_subdirectory(tier1)
add_subdirectory(vscript)
add_subdirectory(vgui2/vgui_controls)

if(SOURCE_SDK_NEW_RESPONSE_SYSTEM)
    add_subdirectory(responserules/runtime)
endif()

if(SOURCE_SDK_BUILD_TOOLS)
    add_subdirectory(utils/vtf2tga)
    add_subdirectory(utils/vtfdiff)
    add_subdirectory(utils/tgadiff)
    add_subdirectory(utils/height2normal)
endif()

if(SOURCE_SDK_BUILD_GAME)
    add_subdirectory(game/server)
    add_subdirectory(game/client)
endif()
