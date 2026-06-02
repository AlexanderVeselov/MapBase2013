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
option(SOURCE_SDK_BUILD_TOOLS "Compatibility umbrella option that enables all tool categories." OFF)
option(SOURCE_SDK_BUILD_MAPTOOLS "Build SDK map compilers and related launcher tools." OFF)
option(SOURCE_SDK_BUILD_MISC_TOOLS "Build standalone SDK utility tools." OFF)
option(SOURCE_SDK_BUILD_SHADERS "Build shader targets." OFF)
option(SOURCE_SDK_USE_LEGACY_SHADERCOMPILE "Run the legacy shadercompile.exe batch pipeline." OFF)
option(SOURCE_SDK_COPY_GAME_DLLS "Copy built game DLLs to SOURCE_SDK_GAME_DLL_OUTPUT_DIR after build." OFF)

set(SOURCE_SDK_GAME_DLL_OUTPUT_DIR "" CACHE PATH "Directory where built game DLLs are copied when SOURCE_SDK_COPY_GAME_DLLS is ON.")

if(SOURCE_SDK_BUILD_TOOLS)
    set(SOURCE_SDK_BUILD_MAPTOOLS ON CACHE BOOL "Build SDK map compilers and related launcher tools." FORCE)
    set(SOURCE_SDK_BUILD_MISC_TOOLS ON CACHE BOOL "Build standalone SDK utility tools." FORCE)
    set(SOURCE_SDK_BUILD_SHADERS ON CACHE BOOL "Build shader targets." FORCE)
endif()

if(SOURCE_SDK_MAPBASE_RPC AND NOT SOURCE_SDK_MAPBASE)
    message(FATAL_ERROR "SOURCE_SDK_MAPBASE_RPC=ON requires SOURCE_SDK_MAPBASE=ON.")
endif()

if(NOT SOURCE_SDK_MAPBASE)
    message(FATAL_ERROR "SOURCE_SDK_MAPBASE=OFF is not supported yet in the current CMake port.")
endif()

if(SOURCE_SDK_COPY_GAME_DLLS AND NOT SOURCE_SDK_GAME_DLL_OUTPUT_DIR)
    message(FATAL_ERROR "SOURCE_SDK_COPY_GAME_DLLS=ON requires SOURCE_SDK_GAME_DLL_OUTPUT_DIR to point at the target game bin directory.")
endif()

if(SOURCE_SDK_USE_LEGACY_SHADERCOMPILE)
    message(FATAL_ERROR "SOURCE_SDK_USE_LEGACY_SHADERCOMPILE=ON is not supported in the current CMake port. The shader build uses the modern DLL-only pipeline and does not invoke shadercompile.exe.")
endif()

set(SOURCE_SDK_ROOT "${CMAKE_SOURCE_DIR}" CACHE INTERNAL "Source SDK src directory.")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This CMake port currently supports Win32 only. Reconfigure with a 32-bit generator, for example: cmake -S . -B build -A Win32")
else()
    set(SOURCE_SDK_PLATFORM_SUBDIR ".")
endif()

set(SOURCE_SDK_PUBLIC_LIB_DIR "${SOURCE_SDK_ROOT}/lib/public/${SOURCE_SDK_PLATFORM_SUBDIR}")
set(SOURCE_SDK_COMMON_LIB_DIR "${SOURCE_SDK_ROOT}/lib/common/${SOURCE_SDK_PLATFORM_SUBDIR}")
set(SOURCE_SDK_BUILD_LIB_DIR "${CMAKE_BINARY_DIR}/lib/public/${SOURCE_SDK_PLATFORM_SUBDIR}")
