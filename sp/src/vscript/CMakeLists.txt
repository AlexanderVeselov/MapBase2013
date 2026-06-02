set(VSCRIPT_SOURCES
    vscript.cpp
    vscript_squirrel.cpp
    vscript_bindings_base.cpp
    vscript_bindings_math.cpp
)

set(VSCRIPT_SQUIRREL_SOURCES
    squirrel/sqstdlib/sqstdaux.cpp
    squirrel/sqstdlib/sqstdblob.cpp
    squirrel/sqstdlib/sqstdio.cpp
    squirrel/sqstdlib/sqstdmath.cpp
    squirrel/sqstdlib/sqstdrex.cpp
    squirrel/sqstdlib/sqstdstream.cpp
    squirrel/sqstdlib/sqstdstring.cpp
    squirrel/sqstdlib/sqstdsystem.cpp
    squirrel/squirrel/sqapi.cpp
    squirrel/squirrel/sqbaselib.cpp
    squirrel/squirrel/sqclass.cpp
    squirrel/squirrel/sqcompiler.cpp
    squirrel/squirrel/sqdebug.cpp
    squirrel/squirrel/sqfuncstate.cpp
    squirrel/squirrel/sqlexer.cpp
    squirrel/squirrel/sqmem.cpp
    squirrel/squirrel/sqobject.cpp
    squirrel/squirrel/sqstate.cpp
    squirrel/squirrel/sqtable.cpp
    squirrel/squirrel/sqvm.cpp
)

set(VSCRIPT_SQDBG_SOURCES
    sqdbg/sqdbg/server.cpp
)

source_sdk_static_library(vscript
    ${VSCRIPT_SOURCES}
    ${VSCRIPT_SQUIRREL_SOURCES}
    ${VSCRIPT_SQDBG_SOURCES}
)

target_include_directories(vscript PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/squirrel/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/squirrel/squirrel"
    "${CMAKE_CURRENT_SOURCE_DIR}/squirrel/sqstdlib"
    "${CMAKE_CURRENT_SOURCE_DIR}/sqdbg/include"
)

if(SOURCE_SDK_MAPBASE_VSCRIPT)
    target_compile_definitions(vscript PRIVATE
        MAPBASE_VSCRIPT
    )
endif()

set_source_files_properties(${VSCRIPT_SQUIRREL_SOURCES} PROPERTIES
    COMPILE_OPTIONS "/wd4100;/wd4611;/wd4127;/wd4244;/wd4702;/wd4706;/wd4800;/wd5033"
)

set_source_files_properties(${VSCRIPT_SQDBG_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS;_WINSOCK_DEPRECATED_NO_WARNINGS"
    COMPILE_OPTIONS "/wd4127;/wd4146;/wd4201;/wd4244;/wd4267;/wd4456;/wd4706"
)
