GLOBAL_CXXFLAGS += -Wno-deprecated-declarations -Werror=switch
# Allow switch-enum to be overridden for files that do not support it, usually because of dependency headers.
ERROR_SWITCH_ENUM = -Werror=switch-enum

$(foreach i, config.h $(wildcard src/lib*/*.hh) $(wildcard src/lib*/*.h $(filter-out %_internal.h, $(wildcard src/lib*c/*.h))), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix, 0644)))

ifdef HOST_UNIX
  $(foreach i, $(wildcard src/lib*/unix/*.hh), \
    $(eval $(call install-file-in, $(i), $(includedir)/nix, 0644)))
endif

$(GCH): src/libutil/util.hh config.h

GCH_CXXFLAGS = $(INCLUDE_libutil)
