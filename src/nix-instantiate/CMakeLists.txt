nix_add_executable(nix-instantiate nix-instantiate.cc)
target_link_libraries(nix-instantiate
	nixexpr nixmain nixstore nixutil nixformat
)
nix_install(TARGETS nix-instantiate DESTINATION bin)
