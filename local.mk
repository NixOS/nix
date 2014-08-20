ifeq ($(MAKECMDGOALS), dist)
  dist-files += $(shell git ls-files)
endif

dist-files += configure config.h.in nix.spec

clean-files += Makefile.config

GLOBAL_CXXFLAGS += -I . -I src -I src/libutil -I src/libstore -I src/libmain -I src/libexpr

$(foreach i, config.h $(call rwildcard, src/lib*, *.hh), $(eval $(call install-file-in, $(i), $(includedir)/nix, 0644)))

$(foreach i, $(call rwildcard, src/boost, *.hpp), $(eval $(call install-file-in, $(i), $(includedir)/nix/$(patsubst src/%/,%,$(dir $(i))), 0644)))
