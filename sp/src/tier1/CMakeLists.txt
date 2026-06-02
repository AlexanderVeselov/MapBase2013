source_sdk_static_library(tier1
    bitbuf.cpp
    newbitbuf.cpp
    byteswap.cpp
    characterset.cpp
    checksum_crc.cpp
    checksum_md5.cpp
    checksum_sha1.cpp
    commandbuffer.cpp
    convar.cpp
    datamanager.cpp
    diff.cpp
    generichash.cpp
    ilocalize.cpp
    interface.cpp
    interval.cpp
    KeyValues.cpp
    kvpacker.cpp
    lzmaDecoder.cpp
    mempool.cpp
    memstack.cpp
    NetAdr.cpp
    splitstring.cpp
    processor_detect.cpp
    rangecheckedvar.cpp
    reliabletimer.cpp
    stringpool.cpp
    strtools.cpp
    tier1.cpp
    tokenreader.cpp
    sparsematrix.cpp
    uniqueid.cpp
    utlbuffer.cpp
    utlbufferutil.cpp
    utlstring.cpp
    utlsymbol.cpp
    snappy.cpp
    snappy-sinksource.cpp
    snappy-stubs-internal.cpp
)

target_compile_definitions(tier1 PRIVATE
    TIER1_STATIC_LIB
)

target_link_libraries(tier1 PRIVATE
    Rpcrt4.lib
)

target_compile_options(tier1 PRIVATE
    /EHsc
)

if(SOURCE_SDK_MAPBASE)
    target_sources(tier1 PRIVATE
        mapbase_con_groups.cpp
        mapbase_matchers_base.cpp
    )
endif()
