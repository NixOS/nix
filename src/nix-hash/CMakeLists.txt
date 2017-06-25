nix_add_executable(nix-hash nix-hash.cc)
target_link_libraries(nix-hash
	nixmain nixstore nixutil nixformat
	${OPENSSL_LIBRARIES} bz2 sqlite3 seccomp ${CURL_LIBRARIES} lzma dl
)
nix_install(TARGETS nix-hash DESTINATION bin)
