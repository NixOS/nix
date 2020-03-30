ifeq ($(MAKECMDGOALS), dist)
  dist-files += $(shell cat .dist-files)
endif

dist-files += configure config.h.in perl/configure

clean-files += Makefile.config

GLOBAL_CXXFLAGS += -Wno-deprecated-declarations

$(foreach i, config.h $(wildcard src/lib*/*.hh), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix, 0644)))

$(GCH) $(PCH): src/libutil/util.hh config.h

GCH_CXXFLAGS = -I src/libutil
