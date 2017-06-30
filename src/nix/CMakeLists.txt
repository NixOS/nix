nix_add_executable(nix
	build.cc
	cat.cc
	command.cc
	copy.cc
	dump-path.cc
	edit.cc
	eval.cc
	hash.cc
	installables.cc
	legacy.cc
	log.cc
	ls.cc
	main.cc
	path-info.cc
	progress-bar.cc
	repl.cc
	run.cc
	show-config.cc
	sigs.cc
	verify.cc
	../linenoise/linenoise.c
)
target_link_libraries(nix
	nixexpr nixmain nixstore nixutil nixformat
)
nix_install(TARGETS nix DESTINATION ${CMAKE_INSTALL_BINDIR})

nix_target_alias(nix nix-hash DESTINATION ${CMAKE_INSTALL_BINDIR})