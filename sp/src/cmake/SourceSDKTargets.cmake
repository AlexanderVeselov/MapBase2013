function(source_sdk_static_library target_name)
    add_library(${target_name} STATIC ${ARGN})

    target_link_libraries(${target_name} PUBLIC source_sdk_base)
    target_compile_definitions(${target_name} PRIVATE "LIBNAME=${target_name}")

    set_target_properties(${target_name} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${SOURCE_SDK_BUILD_LIB_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${SOURCE_SDK_BUILD_LIB_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${SOURCE_SDK_BUILD_LIB_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${SOURCE_SDK_BUILD_LIB_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL "${SOURCE_SDK_BUILD_LIB_DIR}"
    )

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /TP)
        target_link_options(${target_name} PRIVATE /ignore:4221)
    endif()

    source_group(TREE "${SOURCE_SDK_ROOT}" FILES ${ARGN})
endfunction()

function(source_sdk_imported_library target_name)
    if(TARGET ${target_name})
        return()
    endif()

    set(library_path "${SOURCE_SDK_PUBLIC_LIB_DIR}/${target_name}.lib")
    if(NOT EXISTS "${library_path}")
        message(FATAL_ERROR "Missing imported Source SDK library: ${library_path}")
    endif()

    add_library(${target_name} UNKNOWN IMPORTED GLOBAL)
    set_target_properties(${target_name} PROPERTIES
        IMPORTED_LOCATION "${library_path}"
    )
endfunction()

function(source_sdk_console_executable target_name)
    add_executable(${target_name} ${ARGN})

    target_link_libraries(${target_name} PRIVATE
        source_sdk_base
        tier1
    )

    source_sdk_imported_library(tier0)
    source_sdk_imported_library(vstdlib)

    target_link_libraries(${target_name} PRIVATE
        tier0
        vstdlib
        shell32.lib
        user32.lib
        advapi32.lib
        gdi32.lib
        comdlg32.lib
        ole32.lib
    )

    target_compile_definitions(${target_name} PRIVATE
        "EXENAME=${target_name}"
        _CONSOLE
    )

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /TP)
        target_link_options(${target_name} PRIVATE
            /NXCOMPAT
            /ignore:4221
            /NODEFAULTLIB:libc
            /NODEFAULTLIB:libcd
            $<$<CONFIG:Debug>:/NODEFAULTLIB:libcmt>
            $<$<NOT:$<CONFIG:Debug>>:/NODEFAULTLIB:libcmtd>
        )
        set_target_properties(${target_name} PROPERTIES
            LINK_FLAGS "/SUBSYSTEM:CONSOLE"
        )
    endif()

    source_group(TREE "${SOURCE_SDK_ROOT}" FILES ${ARGN})
endfunction()

function(source_sdk_imported_common_library target_name)
    if(TARGET ${target_name})
        return()
    endif()

    set(library_path "${SOURCE_SDK_COMMON_LIB_DIR}/${target_name}.lib")
    if(NOT EXISTS "${library_path}")
        set(library_path "${SOURCE_SDK_ROOT}/lib/common/${target_name}.lib")
    endif()
    if(NOT EXISTS "${library_path}")
        message(FATAL_ERROR "Missing imported Source SDK common library: ${library_path}")
    endif()

    add_library(${target_name} UNKNOWN IMPORTED GLOBAL)
    set_target_properties(${target_name} PROPERTIES
        IMPORTED_LOCATION "${library_path}"
    )
endfunction()

function(source_sdk_shared_library target_name)
    add_library(${target_name} SHARED ${ARGN})

    target_link_libraries(${target_name} PRIVATE
        source_sdk_base
        tier1
    )

    source_sdk_imported_library(tier0)
    source_sdk_imported_library(vstdlib)

    target_link_libraries(${target_name} PRIVATE
        tier0
        vstdlib
        shell32.lib
        user32.lib
        advapi32.lib
        gdi32.lib
        comdlg32.lib
        ole32.lib
        legacy_stdio_definitions.lib
        winmm.lib
    )

    target_compile_definitions(${target_name} PRIVATE
        _WINDOWS
        _USRDLL
        "DLLNAME=${target_name}"
        BINK_VIDEO
        AVI_VIDEO
        WMV_VIDEO
    )

    set_target_properties(${target_name} PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
    )

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /TP /Zm200)
        target_link_options(${target_name} PRIVATE
            /NXCOMPAT
            /ignore:4221
            /NODEFAULTLIB:libc
            /NODEFAULTLIB:libcd
            $<$<CONFIG:Debug>:/NODEFAULTLIB:libcmt>
            $<$<NOT:$<CONFIG:Debug>>:/NODEFAULTLIB:libcmtd>
        )
        set_target_properties(${target_name} PROPERTIES
            LINK_FLAGS "/SUBSYSTEM:WINDOWS"
        )
    endif()

    source_group(TREE "${SOURCE_SDK_ROOT}" FILES ${ARGN})
endfunction()

function(source_sdk_copy_game_dll target_name)
    if(NOT SOURCE_SDK_COPY_GAME_DLLS)
        return()
    endif()

    if(NOT SOURCE_SDK_GAME_DLL_OUTPUT_DIR)
        message(FATAL_ERROR "SOURCE_SDK_COPY_GAME_DLLS=ON requires SOURCE_SDK_GAME_DLL_OUTPUT_DIR.")
    endif()

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SOURCE_SDK_GAME_DLL_OUTPUT_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${target_name}>" "${SOURCE_SDK_GAME_DLL_OUTPUT_DIR}/$<TARGET_FILE_NAME:${target_name}>"
        COMMENT "Copying $<TARGET_FILE_NAME:${target_name}> to ${SOURCE_SDK_GAME_DLL_OUTPUT_DIR}"
        VERBATIM
    )
endfunction()

function(source_sdk_msvc_precompiled_header target_name pch_header pch_source pch_output_name)
    if(NOT MSVC)
        return()
    endif()

    set(no_pch_sources ${ARGN})
    set(pch_output "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${pch_output_name}.pch")

    target_compile_options(${target_name} PRIVATE
        /Yu"${pch_header}"
        /Fp${pch_output}
    )

    set_source_files_properties("${pch_source}" PROPERTIES
        COMPILE_OPTIONS "/Yc${pch_header};/Fp${pch_output}"
    )

    if(no_pch_sources)
        set_source_files_properties(${no_pch_sources} PROPERTIES
            COMPILE_OPTIONS "/Y-"
        )
    endif()
endfunction()
