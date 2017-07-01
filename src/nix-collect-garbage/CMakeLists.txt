nix_add_executable(nix-collect-garbage nix-collect-garbage.cc)
target_link_libraries(nix-collect-garbage
	nixmain nixstore nixutil nixformat
)
nix_install(TARGETS nix-collect-garbage DESTINATION bin)
