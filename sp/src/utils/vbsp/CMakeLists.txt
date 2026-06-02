source_sdk_console_executable(vbsp
    "${SOURCE_SDK_ROOT}/public/tier0/memoverride.cpp"
    boundbox.cpp
    brushbsp.cpp
    "${SOURCE_SDK_ROOT}/public/CollisionUtils.cpp"
    csg.cpp
    cubemap.cpp
    detail.cpp
    detailObjects.cpp
    "${SOURCE_SDK_ROOT}/public/disp_common.cpp"
    disp_ivp.cpp
    "${SOURCE_SDK_ROOT}/public/disp_powerinfo.cpp"
    disp_vbsp.cpp
    faces.cpp
    glfile.cpp
    ivp.cpp
    leakfile.cpp
    "${SOURCE_SDK_ROOT}/public/loadcmdline.cpp"
    "${SOURCE_SDK_ROOT}/public/lumpfiles.cpp"
    map.cpp
    manifest.cpp
    materialpatch.cpp
    materialsub.cpp
    "${SOURCE_SDK_ROOT}/utils/common/mstristrip.cpp"
    nodraw.cpp
    normals.cpp
    overlay.cpp
    "${SOURCE_SDK_ROOT}/utils/common/physdll.cpp"
    portals.cpp
    prtfile.cpp
    "${SOURCE_SDK_ROOT}/public/ScratchPad3D.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/scratchpad_helpers.cpp"
    StaticProp.cpp
    textures.cpp
    tree.cpp
    "${SOURCE_SDK_ROOT}/utils/common/utilmatlib.cpp"
    vbsp.cpp
    worldvertextransitionfixup.cpp
    writebsp.cpp
    "${SOURCE_SDK_ROOT}/public/zip_utils.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/bsplib.cpp"
    "${SOURCE_SDK_ROOT}/public/builddisp.cpp"
    "${SOURCE_SDK_ROOT}/public/ChunkFile.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/cmdlib.cpp"
    "${SOURCE_SDK_ROOT}/public/filesystem_helpers.cpp"
    "${SOURCE_SDK_ROOT}/public/filesystem_init.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/filesystem_tools.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/map_shared.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/pacifier.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/polylib.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/scriplib.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/threads.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/tools_minidump.cpp"
)

if(SOURCE_SDK_MAPBASE_VSCRIPT)
    target_sources(vbsp PRIVATE
        vscript_vbsp.cpp
        vscript_funcs_vmfs.cpp
        vscript_funcs_vis.cpp
    )
endif()

target_include_directories(vbsp PRIVATE
    "${SOURCE_SDK_ROOT}/utils/common"
    "${SOURCE_SDK_ROOT}/utils/vmpi"
)

target_compile_definitions(vbsp PRIVATE
    MACRO_MATHLIB
    PROTECTED_THINGS_DISABLE
)

if(SOURCE_SDK_MAPBASE_VSCRIPT)
    target_compile_definitions(vbsp PRIVATE MAPBASE_VSCRIPT)
endif()

source_sdk_imported_library(bitmap)
source_sdk_imported_library(tier2)
source_sdk_imported_library(vtf)

target_link_libraries(vbsp PRIVATE
    bitmap
    fgdlib
    mathlib
    tier2
    vtf
    ws2_32.lib
    odbc32.lib
    odbccp32.lib
    winmm.lib
)

if(SOURCE_SDK_MAPBASE_VSCRIPT)
    target_link_libraries(vbsp PRIVATE vscript)
endif()