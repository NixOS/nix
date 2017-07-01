nix_add_executable(build-remote
	build-remote.cc
)
target_link_libraries(build-remote
	nixexpr nixmain nixstore nixutil nixformat
)
nix_install(TARGETS build-remote DESTINATION libexec/nix/build-remote)