dist-files += $(shell git ls-files) $(shell git ls-files)

GLOBAL_CXXFLAGS = -I . -I src -I src/libutil -I src/libstore -I src/libmain -I src/libexpr
