nix_add_executable(bsdiff bsdiff.c)
target_link_libraries(bsdiff bz2)

nix_add_executable(bspatch bspatch.c)
target_link_libraries(bspatch bz2)

nix_install(TARGETS bsdiff bspatch DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/nix)
