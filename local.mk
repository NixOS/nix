GLOBAL_CXXFLAGS += -Wno-deprecated-declarations -Werror=switch
# Allow switch-enum to be overridden for files that do not support it, usually because of dependency headers.
ERROR_SWITCH_ENUM = -Werror=switch-enum

$(foreach i, config.h $(wildcard subprojects/lib*/*.hh) $(filter-out %_internal.h, $(wildcard subprojects/lib*c/*.h)), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix, 0644)))

ifdef HOST_UNIX
  $(foreach i, $(wildcard subprojects/lib*/unix/*.hh), \
    $(eval $(call install-file-in, $(i), $(includedir)/nix, 0644)))
endif

$(GCH): subprojects/libutil/util.hh config.h

GCH_CXXFLAGS = $(INCLUDE_libutil)
