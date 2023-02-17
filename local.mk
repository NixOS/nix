clean-files += Makefile.config

GLOBAL_CXXFLAGS += -Wno-deprecated-declarations

$(foreach i, config.h $(wildcard src/lib*/*.hh), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix, 0644)))

$(GCH): src/libutil/include/nix/util/util.hh config.h

GCH_CXXFLAGS = -Isrc/libutil/include/nix/util
