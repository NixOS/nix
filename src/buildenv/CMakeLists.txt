nix_add_executable(buildenv buildenv.cc)
target_link_libraries(buildenv
	nixmain nixstore nixutil nixformat
)
nix_install(TARGETS buildenv DESTINATION libexec/nix)
