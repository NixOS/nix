if(APPLE)
    install(FILES org.nixos.nix-daemon.plist 
        DESTINATION ${CMAKE_INSTALL_PREFIX}/Library/LaunchDaemons)
endif()
