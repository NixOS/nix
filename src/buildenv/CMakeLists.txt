nix_add_executable(buildenv buildenv.cc)
target_link_libraries(buildenv
	nixmain nixstore nixutil nixformat
	#${OPENSSL_LIBRARIES} bz2 sqlite3 seccomp ${CURL_LIBRARIES} lzma dl
)
nix_install(TARGETS buildenv DESTINATION libexec/nix)
