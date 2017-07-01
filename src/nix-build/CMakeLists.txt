nix_add_executable(nix-build nix-build.cc)
target_link_libraries(nix-build
	nixmain nixstore nixutil nixformat
)
nix_install(TARGETS nix-build DESTINATION bin)

nix_target_alias(nix-build nix-shell DESTINATION ${CMAKE_INSTALL_BINDIR})