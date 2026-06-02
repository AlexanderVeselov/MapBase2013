source_sdk_shared_library(vrad_dll
    "${SOURCE_SDK_ROOT}/public/tier0/memoverride.cpp"
    "${SOURCE_SDK_ROOT}/public/BSPTreeData.cpp"
    "${SOURCE_SDK_ROOT}/public/disp_common.cpp"
    "${SOURCE_SDK_ROOT}/public/disp_powerinfo.cpp"
    disp_vrad.cpp
    imagepacker.cpp
    incremental.cpp
    leaf_ambient_lighting.cpp
    lightmap.cpp
    "${SOURCE_SDK_ROOT}/public/loadcmdline.cpp"
    "${SOURCE_SDK_ROOT}/public/lumpfiles.cpp"
    macro_texture.cpp
    "${SOURCE_SDK_ROOT}/utils/common/mpi_stats.cpp"
    mpivrad.cpp
    "${SOURCE_SDK_ROOT}/utils/common/MySqlDatabase.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/pacifier.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/physdll.cpp"
    radial.cpp
    samplehash.cpp
    trace.cpp
    "${SOURCE_SDK_ROOT}/utils/common/utilmatlib.cpp"
    vismat.cpp
    "${SOURCE_SDK_ROOT}/utils/common/vmpi_tools_shared.cpp"
    vrad.cpp
    vrad_dispcoll.cpp
    vraddetailprops.cpp
    vraddisps.cpp
    vraddll.cpp
    vradstaticprops.cpp
    "${SOURCE_SDK_ROOT}/public/zip_utils.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/bsplib.cpp"
    "${SOURCE_SDK_ROOT}/public/builddisp.cpp"
    "${SOURCE_SDK_ROOT}/public/ChunkFile.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/cmdlib.cpp"
    "${SOURCE_SDK_ROOT}/public/DispColl_Common.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/map_shared.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/polylib.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/scriplib.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/threads.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/tools_minidump.cpp"
    "${SOURCE_SDK_ROOT}/public/CollisionUtils.cpp"
    "${SOURCE_SDK_ROOT}/public/filesystem_helpers.cpp"
    "${SOURCE_SDK_ROOT}/public/ScratchPad3D.cpp"
    "${SOURCE_SDK_ROOT}/public/ScratchPadUtils.cpp"
)

target_include_directories(vrad_dll PRIVATE
    "${SOURCE_SDK_ROOT}/utils/common"
    "${SOURCE_SDK_ROOT}/utils/vmpi"
    "${SOURCE_SDK_ROOT}/utils/vmpi/mysql/mysqlpp/include"
    "${SOURCE_SDK_ROOT}/utils/vmpi/mysql/include"
)

target_compile_definitions(vrad_dll PRIVATE
    MPI
    PROTECTED_THINGS_DISABLE
    VRAD
)

source_sdk_imported_library(bitmap)
source_sdk_imported_library(tier2)
source_sdk_imported_library(vmpi)
source_sdk_imported_library(vtf)

target_link_libraries(vrad_dll PRIVATE
    bitmap
    mathlib
    raytrace
    tier2
    vmpi
    vtf
    ws2_32.lib
)