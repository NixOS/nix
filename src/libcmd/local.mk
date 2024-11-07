libraries += libcmd

libcmd_NAME = libnixcmd

libcmd_DIR := $(d)

libcmd_SOURCES := $(wildcard $(d)/*.cc)

libcmd_CXXFLAGS += $(INCLUDE_libutil) $(INCLUDE_libstore) $(INCLUDE_libfetchers) $(INCLUDE_libexpr) $(INCLUDE_libflake) $(INCLUDE_libmain)

libcmd_LDFLAGS = $(EDITLINE_LIBS) $(LOWDOWN_LIBS) $(THREAD_LDFLAGS)

libcmd_LIBS = libutil libstore libfetchers libflake libexpr libmain

$(eval $(call install-file-in, $(buildprefix)$(d)/nix-cmd.pc, $(libdir)/pkgconfig, 0644))
