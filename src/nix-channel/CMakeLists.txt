nix_add_executable(nix-channel nix-channel.cc)
target_link_libraries(nix-channel
	nixmain nixstore nixutil nixformat
)
nix_install(TARGETS nix-channel DESTINATION bin)
