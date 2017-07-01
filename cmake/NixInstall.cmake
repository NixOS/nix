include(CMakeParseArguments)

macro(InstallSymlink _filepath _installdir)
    get_filename_component(_symname ${_filepath} NAME)

    install(CODE "
        if (\"\$ENV{DESTDIR}\" STREQUAL \"\")
            execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink
                            ${_filepath}
                            ${_installdir}/${_symname})
            message(\"-- Created symlink: ${_filepath} -> ${_installdir}/${_symname}\")
        else ()
            execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink
                            ${_filepath}
                            \$ENV{DESTDIR}/${_installdir}/${_symname})
            message(\"-- Created symlink: ${_filepath} -> \$ENV{DESTDIR}/${_installdir}/${_symname}\")
        endif ()
    ")
    install(CODE "")
endmacro(InstallSymlink)

function(nix_install)
    #message(STATUS "=======================nix_install(${ARGN})")
    set(options OPTIONAL FAST)
    set(oneValueArgs DESTINATION RENAME)
    set(multiValueArgs TARGETS CONFIGURATIONS)
    cmake_parse_arguments(NIX_INSTALL "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # message(STATUS "NIX_INSTALL_TARGETS:        ${NIX_INSTALL_TARGETS}")
    # message(STATUS "NIX_INSTALL_DESTINATION:    ${NIX_INSTALL_DESTINATION}")
    # message(STATUS "NIX_INSTALL_OPTIONAL:       ${NIX_INSTALL_OPTIONAL}")
    # message(STATUS "NIX_INSTALL_FAST:           ${NIX_INSTALL_FAST}")
    # message(STATUS "NIX_INSTALL_RENAME:         ${NIX_INSTALL_RENAME}")
    # message(STATUS "NIX_INSTALL_CONFIGURATIONS: ${NIX_INSTALL_CONFIGURATIONS}")

    # TODO: finish conditional parameters    
    install(
         TARGETS         ${NIX_INSTALL_TARGETS}
         DESTINATION     ${NIX_INSTALL_DESTINATION}
    #     OPTIONAL        ${NIX_INSTALL_OPTIONAL}
    #     FAST            ${NIX_INSTALL_FAST}
    #     RENAME          ${NIX_INSTALL_RENAME}
    #     CONFIGURATIONS  ${NIX_INSTALL_CONFIGURATIONS}
    )
    foreach(_target ${NIX_INSTALL_TARGETS})
        #message(STATUS "nix_install ${_target}")
        get_property(is_externalized TARGET ${_target} PROPERTY EXTERNALIZED)
        if(is_externalized)
            #message(STATUS "${_target} is externalized")
            get_property(target_location TARGET ${_target} PROPERTY LOCATION)
            InstallSymlink(${target_location}.debug ${CMAKE_INSTALL_PREFIX}/${NIX_INSTALL_DESTINATION})
        endif()
    endforeach()
endfunction()
