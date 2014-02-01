dist-files += $(shell git ls-files) $(shell git ls-files)

dist-files += configure config.h.in

GLOBAL_CXXFLAGS += -I . -I src -I src/libutil -I src/libstore -I src/libmain -I src/libexpr
