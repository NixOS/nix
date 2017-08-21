nix_add_library(nixformat SHARED 
	free_funcs.cc
	parsing.cc
	format_implementation.cc
)
nix_install(TARGETS nixformat DESTINATION ${CMAKE_INSTALL_LIBDIR})
