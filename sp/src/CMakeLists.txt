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

if(SOURCE_SDK_BUILD_GAME AND SOURCE_SDK_NEW_RESPONSE_SYSTEM)
    add_subdirectory(responserules/runtime)
endif()

if(SOURCE_SDK_BUILD_MISC_TOOLS)
    add_subdirectory(utils/captioncompiler)
    add_subdirectory(utils/motionmapper)
    add_subdirectory(utils/serverplugin_sample)
    add_subdirectory(utils/vtf2tga)
    add_subdirectory(utils/vtfdiff)
    add_subdirectory(utils/tgadiff)
    add_subdirectory(utils/height2normal)
endif()

if(SOURCE_SDK_BUILD_MAPTOOLS)
    add_subdirectory(utils/vbsp)
    add_subdirectory(utils/vrad)
    add_subdirectory(utils/vrad_launcher)
    add_subdirectory(utils/vvis)
    add_subdirectory(utils/vvis_launcher)
endif()

if(SOURCE_SDK_BUILD_SHADERS)
    add_subdirectory(materialsystem/stdshaders)
endif()

if(SOURCE_SDK_BUILD_GAME)
    add_subdirectory(game/server)
    add_subdirectory(game/client)
endif()
