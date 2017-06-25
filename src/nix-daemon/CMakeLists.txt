nix_add_executable(nix-daemon nix-daemon.cc)
target_link_libraries(nix-daemon
	nixmain nixstore nixutil nixformat
	${OPENSSL_LIBRARIES} bz2 sqlite3 seccomp ${CURL_LIBRARIES} lzma dl pthread
)
nix_install(TARGETS nix-daemon DESTINATION bin)