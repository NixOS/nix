nix_add_executable(nix-env
	nix-env.cc
	user-env.cc
)
target_link_libraries(nix-env
	nixexpr nixmain nixstore nixutil nixformat
    ${OPENSSL_LIBRARIES} bz2 sqlite3 seccomp ${CURL_LIBRARIES} lzma dl pthread
)
nix_install(TARGETS nix-env DESTINATION bin)
