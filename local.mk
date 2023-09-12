clean-files += Makefile.config

GLOBAL_CXXFLAGS += -Wno-deprecated-declarations -Werror=switch
# Allow switch-enum to be overridden for files that do not support it, usually because of dependency headers.
ERROR_SWITCH_ENUM = -Werror=switch-enum

$(foreach i, config.h $(wildcard src/lib*/*.hh), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix, 0644)))

$(GCH): src/libutil/util.hh config.h

GCH_CXXFLAGS = -I src/libutil
