LIBS=-larchive -lcrypto -llzma -lbz2 -lz -lbrotlienc -lbrotlidec
unit-tests:
	echo $(nix_LDFLAGS)
	$(CXX) -o unit-test $(nix_CXXFLAGS) $(nix_LDFLAGS) $(LIBS) --std=c++17 $$(pkg-config --libs gtest_main) ./src/libutil/*.o tests/unit-tests/test-util.cc
