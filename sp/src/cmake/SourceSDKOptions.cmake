set(SOURCE_SDK_GAME "episodic" CACHE STRING "Single-player game variant to configure.")
set_property(CACHE SOURCE_SDK_GAME PROPERTY STRINGS hl2 episodic)

option(SOURCE_SDK_MAPBASE "Enable Mapbase code paths." ON)
option(SOURCE_SDK_MAPBASE_RPC "Enable Mapbase Discord RPC code paths." OFF)
option(SOURCE_SDK_MAPBASE_VSCRIPT "Enable Mapbase VScript implementation." ON)
option(SOURCE_SDK_NEW_RESPONSE_SYSTEM "Enable the new response system." ON)
option(SOURCE_SDK_RENDER_NEW "Enable the experimental render_new client sources." ON)
option(SOURCE_SDK_SIXENSE "Enable Sixense input support sources." ON)
option(SOURCE_SDK_SOURCESDK "Enable Source SDK public-release code paths." ON)
option(SOURCE_SDK_BUILD_GAME "Build SP client/server game DLLs." OFF)
option(SOURCE_SDK_BUILD_TOOLS "Build SDK tools." OFF)
option(SOURCE_SDK_COPY_GAME_DLLS "Copy built game DLLs to SOURCE_SDK_GAME_DLL_OUTPUT_DIR after build." OFF)

set(SOURCE_SDK_GAME_DLL_OUTPUT_DIR "" CACHE PATH "Directory where built game DLLs are copied when SOURCE_SDK_COPY_GAME_DLLS is ON.")

if(NOT SOURCE_SDK_MAPBASE)
    message(FATAL_ERROR "SOURCE_SDK_MAPBASE=OFF is not supported yet: the current vscript headers still require Mapbase VScript types.")
endif()

if(SOURCE_SDK_COPY_GAME_DLLS AND NOT SOURCE_SDK_GAME_DLL_OUTPUT_DIR)
    message(FATAL_ERROR "SOURCE_SDK_COPY_GAME_DLLS=ON requires SOURCE_SDK_GAME_DLL_OUTPUT_DIR to point at the target game bin directory.")
endif()

set(SOURCE_SDK_ROOT "${CMAKE_SOURCE_DIR}" CACHE INTERNAL "Source SDK src directory.")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(SOURCE_SDK_PLATFORM_SUBDIR "x64")
else()
    set(SOURCE_SDK_PLATFORM_SUBDIR ".")
endif()

set(SOURCE_SDK_PUBLIC_LIB_DIR "${SOURCE_SDK_ROOT}/lib/public/${SOURCE_SDK_PLATFORM_SUBDIR}")
set(SOURCE_SDK_COMMON_LIB_DIR "${SOURCE_SDK_ROOT}/lib/common/${SOURCE_SDK_PLATFORM_SUBDIR}")
set(SOURCE_SDK_BUILD_LIB_DIR "${CMAKE_BINARY_DIR}/lib/public/${SOURCE_SDK_PLATFORM_SUBDIR}")
