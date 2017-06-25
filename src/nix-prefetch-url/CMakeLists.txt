nix_add_executable(nix-prefetch-url nix-prefetch-url.cc)
target_link_libraries(nix-prefetch-url
	nixmain nixexpr nixstore nixutil nixformat
	${OPENSSL_LIBRARIES} bz2 sqlite3 seccomp ${CURL_LIBRARIES} lzma dl
)
nix_install(TARGETS nix-prefetch-url DESTINATION bin)
