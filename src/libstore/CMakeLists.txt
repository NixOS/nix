add_library(nixstore SHARED
    store-api.cc
    pathlocks.cc
    local-store.cc
    references.cc
    optimise-store.cc
    misc.cc
    derivations.cc
    build.cc
    gc.cc
    profiles.cc
    globals.cc
    download.cc
    builtins.cc
    remote-store.cc
)

target_compile_definitions(nixstore PRIVATE
    NIX_PREFIX="${CMAKE_INSTALL_PREFIX}"
    NIX_STORE_DIR="${NIX_STORE_DIR}"
    NIX_DATA_DIR="${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}"
    NIX_STATE_DIR="${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LOCALSTATEDIR}/nix"
    NIX_LOG_DIR="${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LOCALSTATEDIR}/log/nix"
    NIX_CONF_DIR="${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_SYSCONFDIR}/nix"
    NIX_LIBEXEC_DIR="${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBEXECDIR}"
    NIX_BIN_DIR="${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}"
    BASH_PATH="${bash}"
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/schema.sql.hh
    DEPENDS schema.sql
    COMMAND sed -e "s/\"/\\\\\"/g"
                -e "s/\\\(.*\\\)/\"\\1\"\\n/"
                < schema.sql
                > schema.sql.hh
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating schema.sql.hh"
    VERBATIM
)
set_property(SOURCE local-store.cc APPEND PROPERTY OBJECT_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/schema.sql.hh)

nix_install(TARGETS nixstore DESTINATION ${CMAKE_INSTALL_LIBDIR})

configure_file(nix-store.pc.in nix-store.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/nix-store.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ # 0644
)

configure_file(sandbox-defaults.sb.in sandbox-defaults.sb @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/sandbox-defaults.sb DESTINATION ${CMAKE_INSTALL_DATADIR}/nix
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ # 0644
)
