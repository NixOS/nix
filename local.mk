ifeq ($(MAKECMDGOALS), dist)
  dist-files += $(shell git ls-files) $(shell git ls-files)
endif

dist-files += configure config.h.in nix.spec

GLOBAL_CXXFLAGS += -I . -I src -I src/libutil -I src/libstore -I src/libmain -I src/libexpr

$(foreach i, config.h $(call rwildcard, src/lib*, *.hh), $(eval $(call install-file-in, $(i), $(includedir)/nix, 0644)))
