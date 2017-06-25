include(CMakeParseArguments)

define_property(TARGET PROPERTY EXTERNALIZED
    BRIEF_DOCS "Externalize debuginfo"
    FULL_DOCS "void")

# Lifted from LLVM cmake code
function(nix_externalize_debuginfo name)
    if(NOT NIX_EXTERNALIZE_DEBUGINFO)
        return()
    endif()

    if(NOT NIX_EXTERNALIZE_DEBUGINFO_SKIP_STRIP)
        if(APPLE)
            set(strip_command COMMAND xcrun strip -Sxl $<TARGET_FILE:${name}>)
        else()
            set(strip_command COMMAND strip -gx $<TARGET_FILE:${name}>)
        endif()
    endif()

    if(APPLE)
        if(CMAKE_CXX_FLAGS MATCHES "-flto"
            OR CMAKE_CXX_FLAGS_${uppercase_CMAKE_BUILD_TYPE} MATCHES "-flto")

            set(lto_object ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/${name}-lto.o)
            set_property(TARGET ${name} APPEND_STRING PROPERTY
                LINK_FLAGS " -Wl,-object_path_lto,${lto_object}")
        endif()
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND xcrun dsymutil $<TARGET_FILE:${name}> ${strip_command}
            )
    else()
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND objcopy --only-keep-debug $<TARGET_FILE:${name}> 
                $<TARGET_FILE:${name}>.debug
                ${strip_command} -R .gnu_debuglink
            COMMAND objcopy --add-gnu-debuglink=$<TARGET_FILE:${name}>.debug 
                $<TARGET_FILE:${name}>
        )
    endif()
    set_property(TARGET ${name} APPEND_STRING PROPERTY
        EXTERNALIZED "true")
endfunction()

function(nix_add_library)
    cmake_parse_arguments(ARG "STATIC;SHARED" "" "" ${ARGN})
    list(GET ARGN 0 name)
    list(REMOVE_AT ARGN 0)
    unset(extra_args)
    if(ARG_STATIC)
      	set(extra_args STATIC)
    elseif(ARG_SHARED)
      	set(extra_args SHARED)
    endif()

    add_library(${name} ${extra_args} ${ARGN})
    nix_externalize_debuginfo(${name})
endfunction()

function(nix_add_executable)
    cmake_parse_arguments(ARG "" "" "" ${ARGN})
    list(GET ARGN 0 name)
    list(REMOVE_AT ARGN 0)
    add_executable(${name} ${ARGN})
    nix_externalize_debuginfo(${name})
endfunction()