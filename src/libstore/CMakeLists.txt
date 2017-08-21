add_library(nixstore SHARED
    binary-cache-store.cc
    build.cc
    builtins.cc
    crypto.cc
    derivations.cc
    download.cc
    export-import.cc
    gc.cc
    globals.cc
    http-binary-cache-store.cc
    legacy-ssh-store.cc
    local-binary-cache-store.cc
    local-fs-store.cc
    local-store.cc
    machines.cc
    misc.cc
    nar-accessor.cc
    nar-info.cc
    nar-info-disk-cache.cc
    optimise-store.cc
    pathlocks.cc
    profiles.cc
    references.cc
    remote-fs-accessor.cc
    remote-store.cc
    s3-binary-cache-store.cc
    sqlite.cc
    ssh.cc
    ssh-store.cc
    store-api.cc
)

target_link_libraries(nixstore
    nixutil nixformat
    ${LIBSECCOMP_LIBRARIES}
    ${LIBSODIUM_LIBRARIES}
    ${CURL_LIBRARIES}
    ${SQLITE3_LIBRARIES}
    ${LIBBZ2}
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
    SANDBOX_SHELL="${NIX_SANDBOX_SHELL}"
    LSOF="${lsof}"
)

set_property(SOURCE local-store.cc APPEND PROPERTY OBJECT_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/schema.sql.gen.hh)

set(sandbox_headers sandbox-defaults.sb.gen.hh sandbox-network.sb.gen.hh sandbox-minimal.sb.gen.hh)
prepend_path(${CMAKE_CURRENT_SOURCE_DIR} sandbox_headers)

set_property(SOURCE build.cc APPEND PROPERTY OBJECT_DEPENDS ${sandbox_headers})

# Malformed file will fail with errors in sqlite tables, i.e.:
# nix-store --init
#   error: creating statement: no such table: FailedPaths
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/schema.sql.gen.hh
    DEPENDS schema.sql
    COMMAND sed -e "s/\"/\\\\\"/g"
                -e "s/\\\(.*\\\)/\"\\1\\\\n\"/"
                < schema.sql
                > schema.sql.gen.hh
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating schema.sql.gen.hh"
    VERBATIM
)

function(gen_sandbox_headers _target _depends)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/${_target}
        DEPENDS ${_depends}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        
        COMMAND echo "R\"foo(" >> ${_target}.tmp
        COMMAND cat ${_depends} >> ${_target}.tmp
        COMMAND echo ")foo\"" >> ${_target}.tmp
        COMMAND mv ${_target}.tmp ${_target}
        VERBATIM
    )
endfunction()

gen_sandbox_headers(sandbox-defaults.sb.gen.hh schema.sql.gen.hh)
gen_sandbox_headers(sandbox-network.sb.gen.hh schema.sql.gen.hh)
gen_sandbox_headers(sandbox-minimal.sb.gen.hh schema.sql.gen.hh)

nix_install(TARGETS nixstore DESTINATION ${CMAKE_INSTALL_LIBDIR})

configure_file(nix-store.pc.in nix-store.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/nix-store.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ # 0644
)
