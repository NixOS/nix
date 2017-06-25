set(nix_bin_scripts
    nix-build
    nix-channel
    nix-copy-closure
    nix-generate-patches
    nix-install-package
    nix-pull
    nix-push
)
#list(APPEND bin_scripts ${nix_bin_scripts})
foreach(prog ${nix_bin_scripts})
    install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/${prog}
        DESTINATION ${CMAKE_INSTALL_BINDIR})
endforeach()

set(nix_substituters
    copy-from-other-stores.pl
    download-from-binary-cache.pl
    download-using-manifests.pl
)

set(nix_noinst_scripts
    build-remote.pl
    find-runtime-roots.pl
    resolve-system-dependencies.pl
    nix-http-export.cgi
    nix-profile.sh
    nix-reduce-build
    ${nix_substituters}
    )

list(APPEND noinst_scripts ${nix_noinst_scripts})

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nix-build.in nix-build @ONLY)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nix-profile.sh.in nix-profile.sh @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/build-remote.pl.in               build-remote.pl @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/copy-from-other-stores.pl.in     copy-from-other-stores.pl   @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/download-from-binary-cache.pl.in download-from-binary-cache.pl   @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/download-using-manifests.pl.in   download-using-manifests.pl @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/find-runtime-roots.pl.in         find-runtime-roots.pl   @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nix-channel.in                   nix-channel @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nix-copy-closure.in              nix-copy-closure    @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nix-generate-patches.in          nix-generate-patches    @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nix-http-export.cgi.in           nix-http-export.cgi @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nix-install-package.in           nix-install-package @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nix-profile.sh.in                nix-profile.sh  @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nix-pull.in                      nix-pull    @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nix-push.in                      nix-push    @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nix-reduce-build.in              nix-reduce-build    @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/resolve-system-dependencies.pl.in resolve-system-dependencies.pl @ONLY)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/nix-profile.sh
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/profile.d
        RENAME nix.sh
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ # 0644
)
install(PROGRAMS
    ${CMAKE_CURRENT_BINARY_DIR}/find-runtime-roots.pl
    ${CMAKE_CURRENT_BINARY_DIR}/build-remote.pl
    ${CMAKE_CURRENT_BINARY_DIR}/resolve-system-dependencies.pl
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/nix
)

foreach(prog ${nix_substituters})
    install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/${prog}
        DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/nix/substituters)
endforeach()

add_custom_command( OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/nix-shell
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/nix-build
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMAND ln -s nix-build nix-shell
)
add_custom_target( dummy_target_xxx ALL 
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/nix-shell
)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/nix-shell 
    DESTINATION ${CMAKE_INSTALL_BINDIR})
