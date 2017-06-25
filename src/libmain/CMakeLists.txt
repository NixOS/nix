nix_add_library(nixmain SHARED
	shared.cc
	stack.cc
)

nix_install(TARGETS nixmain DESTINATION ${CMAKE_INSTALL_LIBDIR})

#libmain_ALLOW_UNDEFINED = 1
set_target_properties(nixmain PROPERTIES LINK_FLAGS -Wl,-z,defs)

target_link_libraries(nixmain
    nixstore nixutil nixformat
	${OPENSSL_LIBRARIES}
	bz2
	sqlite3
	seccomp
	${CURL_LIBRARIES}
	lzma
	dl
)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nix-main.pc.in nix-main.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/nix-main.pc
	DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/pkgconfig)
