clean-files += Makefile.config

GLOBAL_CXXFLAGS += -Wno-deprecated-declarations

$(eval $(call install-file-in, config.h, $(includedir)/nix, 0644))

$(GCH): src/libutil/include/nix/util/util.hh config.h

GCH_CXXFLAGS = -Isrc/libutil/include/nix/util
