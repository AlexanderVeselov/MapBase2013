source_sdk_static_library(fgdlib
    gamedata.cpp
    gdclass.cpp
    gdvar.cpp
    inputoutput.cpp
    wckeyvalues.cpp
)

target_include_directories(fgdlib PRIVATE
    "${SOURCE_SDK_ROOT}/utils/common"
)
