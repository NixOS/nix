nix_add_executable(nix-store
    xmlgraph.cc
    dotgraph.cc
    nix-store.cc
)
target_link_libraries(nix-store
    nixmain nixstore nixutil nixformat
    ${OPENSSL_LIBRARIES} bz2 sqlite3 seccomp ${CURL_LIBRARIES} lzma dl pthread
)
if(${LIBSODIUM_FOUND})
    target_link_libraries(nix-store sodium)
endif()

target_compile_definitions(nix-store PRIVATE CURL="${CURL}")
nix_install(TARGETS nix-store DESTINATION bin)