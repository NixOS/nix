nix_add_library(nixutil SHARED
	compression.cc
	affinity.cc
	xml-writer.cc
	util.cc
	serialise.cc
	archive.cc
	regex.cc
	hash.cc
)
nix_install(TARGETS nixutil DESTINATION ${CMAKE_INSTALL_LIBDIR})
