nix_add_executable(nix-collect-garbage nix-collect-garbage.cc)
target_link_libraries(nix-collect-garbage
	nixmain nixstore nixutil nixformat
	${OPENSSL_LIBRARIES} bz2 sqlite3 seccomp ${CURL_LIBRARIES} lzma dl
)
nix_install(TARGETS nix-collect-garbage DESTINATION bin)