#ifeq ($(OS), Darwin)

nix_add_executable(resolve-system-dependencies resolve-system-dependencies.cc)
target_link_libraries(resolve-system-dependencies
	nixmain nixexpr nixstore nixutil nixformat
)
nix_install(TARGETS resolve-system-dependencies DESTINATION bin)
