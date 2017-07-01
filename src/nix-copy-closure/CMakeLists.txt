nix_add_executable(nix-copy-closure nix-copy-closure.cc)
target_link_libraries(nix-copy-closure
	nixmain nixstore nixutil nixformat
)
nix_install(TARGETS nix-copy-closure DESTINATION bin)
