source_sdk_shared_library(vvis_dll NO_MEMOVERRIDE
    "${SOURCE_SDK_ROOT}/utils/common/bsplib.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/cmdlib.cpp"
    "${SOURCE_SDK_ROOT}/public/collisionutils.cpp"
    "${SOURCE_SDK_ROOT}/public/filesystem_helpers.cpp"
    flow.cpp
    "${SOURCE_SDK_ROOT}/public/loadcmdline.cpp"
    "${SOURCE_SDK_ROOT}/public/lumpfiles.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/mpi_stats.cpp"
    mpivis.cpp
    "${SOURCE_SDK_ROOT}/utils/common/MySqlDatabase.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/pacifier.cpp"
    "${SOURCE_SDK_ROOT}/public/scratchpad3d.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/scratchpad_helpers.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/scriplib.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/threads.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/tools_minidump.cpp"
    "${SOURCE_SDK_ROOT}/utils/common/vmpi_tools_shared.cpp"
    vvis.cpp
    WaterDist.cpp
    "${SOURCE_SDK_ROOT}/public/zip_utils.cpp"
)

target_include_directories(vvis_dll PRIVATE
    "${SOURCE_SDK_ROOT}/utils/common"
    "${SOURCE_SDK_ROOT}/utils/vmpi"
    "${SOURCE_SDK_ROOT}/utils/vmpi/mysql/include"
)

target_compile_definitions(vvis_dll PRIVATE
    MPI
    PROTECTED_THINGS_DISABLE
)

source_sdk_imported_library(tier2)
source_sdk_imported_library(vmpi)

target_link_libraries(vvis_dll PRIVATE
    mathlib
    tier2
    vmpi
    odbc32.lib
    odbccp32.lib
    ws2_32.lib
)