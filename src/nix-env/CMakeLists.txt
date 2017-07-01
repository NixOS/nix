nix_add_executable(nix-env
	nix-env.cc
	user-env.cc
)
target_link_libraries(nix-env
	nixexpr nixmain nixstore nixutil nixformat
)
nix_install(TARGETS nix-env DESTINATION bin)
