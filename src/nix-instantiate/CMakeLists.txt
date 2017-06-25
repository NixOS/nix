nix_add_executable(nix-instantiate nix-instantiate.cc)
target_link_libraries(nix-instantiate
	nixexpr nixmain nixstore nixutil nixformat
	${OPENSSL_LIBRARIES} bz2 sqlite3 seccomp ${CURL_LIBRARIES} lzma dl
)
nix_install(TARGETS nix-instantiate DESTINATION bin)
