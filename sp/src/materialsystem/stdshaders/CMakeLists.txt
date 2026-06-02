set(STDSHADERS_DX9_SOURCES
    BaseVSShader.cpp
    Bloom.cpp
    cloak_blended_pass_helper.cpp
    core_dx9.cpp
    decalmodulate_dx9.cpp
    depthoffield_dx9.cpp
    depthwrite.cpp
    emissive_scroll_blended_pass_helper.cpp
    engine_post_dx9.cpp
    example_model_dx9.cpp
    example_model_dx9_helper.cpp
    eye_refract.cpp
    eye_refract_helper.cpp
    eyeball.cpp
    eyeglint_dx9.cpp
    eyes_dx8_dx9_helper.cpp
    eyes_dx9.cpp
    flesh_interior_blended_pass_helper.cpp
    lightmappedgeneric_dx9.cpp
    lightmappedgeneric_dx9_helper.cpp
    lightmappedreflective.cpp
    "${SOURCE_SDK_ROOT}/public/tier0/memoverride.cpp"
    MonitorScreen_dx9.cpp
    refract.cpp
    refract_dx9_helper.cpp
    screenspace_general.cpp
    shatteredglass.cpp
    skin_dx9_helper.cpp
    splinerope.cpp
    sprite_dx9.cpp
    teeth.cpp
    unlitgeneric_dx9.cpp
    unlittwotexture_dx9.cpp
    vertexlitgeneric_dx9.cpp
    vertexlitgeneric_dx9_helper.cpp
    water.cpp
    windowimposter_dx90.cpp
    worldtwotextureblend.cpp
    worldvertextransition.cpp
    worldvertextransition_dx8_helper.cpp
)

if(SOURCE_SDK_GAME STREQUAL "episodic")
    set(STDSHADERS_GAME_CAPS EPISODIC)
    set(STDSHADERS_VPC_GAME episodic)
elseif(SOURCE_SDK_GAME STREQUAL "hl2")
    set(STDSHADERS_GAME_CAPS VALVE)
    set(STDSHADERS_VPC_GAME valve)
else()
    message(FATAL_ERROR "Unsupported SOURCE_SDK_GAME=${SOURCE_SDK_GAME}. Expected episodic or hl2.")
endif()

message(STATUS "Building game_shader_dx9 for ${SOURCE_SDK_GAME} with the modern CMake pipeline. The legacy shadercompile.exe batch pipeline is intentionally disabled.")

source_sdk_shared_library(game_shader_dx9
    ${STDSHADERS_DX9_SOURCES}
)

target_include_directories(game_shader_dx9 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fxctmp9"
    "${CMAKE_CURRENT_SOURCE_DIR}/vshtmp9"
)

target_compile_definitions(game_shader_dx9 PRIVATE
    STDSHADER_DX9_DLL_EXPORT
    FAST_MATERIALVAR_ACCESS
    USE_ACTUAL_DX
    SOURCE1=1
    _EXTERNAL_DLL_EXT=.dll
    VPCGAMECAPS=${STDSHADERS_GAME_CAPS}
    VPCGAME=${STDSHADERS_VPC_GAME}
)

if(SOURCE_SDK_MAPBASE)
    target_compile_definitions(game_shader_dx9 PRIVATE PARALLAX_CORRECTED_CUBEMAPS)
endif()

source_sdk_imported_library(shaderlib)

target_link_libraries(game_shader_dx9 PRIVATE
    mathlib
    shaderlib
    version.lib
)

set_source_files_properties("${SOURCE_SDK_ROOT}/public/tier0/memoverride.cpp" PROPERTIES
    COMPILE_OPTIONS "/Y-"
)