configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.nix.in config.nix @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/config.nix DESTINATION ${CMAKE_INSTALL_DATADIR}/nix/corepkgs)

set(corepkgs_FILES
	nar.nix
	buildenv.nix
	buildenv.pl
	unpack-channel.nix
	derivation.nix
	fetchurl.nix
	imported-drv-to-derivation.nix
)

foreach(file ${corepkgs_FILES})
    install(FILES ${file} DESTINATION ${CMAKE_INSTALL_DATADIR}/nix/corepkgs)
endforeach()
