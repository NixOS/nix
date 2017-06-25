nix_add_executable(download-via-ssh download-via-ssh.cc)
target_include_directories(download-via-ssh PRIVATE 
    "${CMAKE_SOURCE_DIR}/src/nix-store"
)
target_link_libraries(download-via-ssh
    nixmain nixstore nixutil nixformat
    ${OPENSSL_LIBRARIES} bz2 sqlite3 seccomp ${CURL_LIBRARIES} lzma
)
nix_install(TARGETS download-via-ssh 
	DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/nix/substituters)
