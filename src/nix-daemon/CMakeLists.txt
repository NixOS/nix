nix_add_executable(nix-daemon nix-daemon.cc)
target_link_libraries(nix-daemon
	nixmain nixstore nixutil nixformat
)
nix_install(TARGETS nix-daemon DESTINATION bin)
