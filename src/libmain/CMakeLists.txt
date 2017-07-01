nix_add_library(nixmain SHARED
	common-args.cc
	shared.cc
	stack.cc
)

nix_install(TARGETS nixmain DESTINATION ${CMAKE_INSTALL_LIBDIR})

#set_target_properties(nixmain PROPERTIES LINK_FLAGS -Wl,-z,defs)

target_link_libraries(nixmain
    nixstore nixutil nixformat
)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nix-main.pc.in nix-main.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/nix-main.pc
	DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/pkgconfig)
