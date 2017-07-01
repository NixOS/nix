nix_add_library(nixutil SHARED
	affinity.cc
	archive.cc
	args.cc
	compression.cc
	config.cc
	hash.cc
	json.cc
	logging.cc
	serialise.cc
	thread-pool.cc
	util.cc
	xml-writer.cc

)

target_compile_definitions(nixutil PRIVATE BRO="${bro}")

target_link_libraries(nixutil
	nixformat
	${OPENSSL_LIBRARIES}
	lzma
	bz2
	pthread
)

nix_install(TARGETS nixutil DESTINATION ${CMAKE_INSTALL_LIBDIR})
