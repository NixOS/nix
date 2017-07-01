nix_add_executable(nix-prefetch-url nix-prefetch-url.cc)
target_link_libraries(nix-prefetch-url
	nixmain nixexpr nixstore nixutil nixformat
)
nix_install(TARGETS nix-prefetch-url DESTINATION bin)
