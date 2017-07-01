set(nix_noinst_scripts
    nix-http-export.cgi
    nix-profile.sh
    nix-reduce-build
    )

list(APPEND noinst_scripts ${nix_noinst_scripts})

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nix-profile.sh.in        nix-profile.sh @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nix-http-export.cgi.in   nix-http-export.cgi @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nix-profile.sh.in        nix-profile.sh  @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nix-reduce-build.in      nix-reduce-build    @ONLY)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/nix-profile.sh
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/profile.d
        RENAME nix.sh
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ # 0644
)
