#if(Linux)
	configure_file(nix-daemon.socket.in nix-daemon.socket @ONLY)
	configure_file(nix-daemon.service.in nix-daemon.service @ONLY)
    install(FILES
    	${CMAKE_CURRENT_BINARY_DIR}/nix-daemon.socket
    	${CMAKE_CURRENT_BINARY_DIR}/nix-daemon.service 
        DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system)
#endif()
