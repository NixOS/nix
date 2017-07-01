#if(LINUX)
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nix-daemon.conf.in nix-daemon.conf @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/nix-daemon.conf
    	DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/init)
#endif()
