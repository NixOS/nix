set(nix_perl_modules
  lib/Nix/Store.pm
  lib/Nix/Manifest.pm
  lib/Nix/GeneratePatches.pm
  lib/Nix/SSH.pm
  lib/Nix/CopyClosure.pm
  lib/Nix/Utils.pm
)

configure_file(lib/Nix/Config.pm.in Config.pm @ONLY)
list(APPEND nix_perl_modules ${CMAKE_CURRENT_BINARY_DIR}/Config.pm)

foreach(file ${nix_perl_modules})
    install(FILES ${file} DESTINATION ${perllibdir}/Nix)
endforeach()

if("${perlbindings}" STREQUAL "YES")

    add_custom_command( OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Store.cc
        DEPENDS lib/Nix/Store.xs
        COMMAND xsubpp lib/Nix/Store.xs -output ${CMAKE_CURRENT_BINARY_DIR}/Store.cc
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating Store.cc"
    )

    nix_add_library(Store SHARED ${CMAKE_CURRENT_BINARY_DIR}/Store.cc)
    set_target_properties(Store PROPERTIES PREFIX "")

    target_compile_definitions(Store PRIVATE
        _FILE_OFFSET_BITS=64
    )

    target_compile_options(Store PRIVATE
        -Wno-unknown-warning-option
        -Wno-unused-variable
        -Wno-literal-suffix
        -Wno-reserved-user-defined-literal
        -Wno-duplicate-decl-specifier
        -Wno-pointer-bool-conversion
    )

    execute_process(COMMAND perl -e "use Config; print \$Config{archlibexp};" OUTPUT_VARIABLE perlarchlibexp)
    target_include_directories(Store PRIVATE
        ${perlarchlibexp}/CORE
    )
    
    if(CYGWIN)
        execute_process(COMMAND perl -e "use Config; print \$Config{archlib};" OUTPUT_VARIABLE archlib)
        execute_process(COMMAND perl -e "use Config; print \$Config{libperl};" OUTPUT_VARIABLE libperl)
        execute_process(COMMAND find ${archlib} -name ${libperl} OUTPUT_VARIABLE perlib_loc)
        set_target_properties(Store PROPERTIES LINK_FLAGS ${PERL_LIBRARY}) # ${perlib_loc}
    endif()

    target_link_libraries(Store
        nixstore nixutil
        ${SODIUM_LIBS}
        dl
    )
    nix_install(TARGETS Store DESTINATION ${perllibdir}/auto/Nix/Store)

endif()
