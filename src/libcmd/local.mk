libraries += libcmd

libcmd_NAME = libnixcmd

libcmd_DIR := $(d)

libcmd_SOURCES := $(wildcard $(d)/*.cc)

libcmd_CXXFLAGS += -I src/libutil -I src/libstore -I src/libexpr -I src/libmain -I src/libfetchers -I src/libstore-cmd

libcmd_LDFLAGS = $(EDITLINE_LIBS) $(LOWDOWN_LIBS) -pthread

libcmd_LIBS = libstore libutil libexpr libmain libfetchers libstore-cmd

$(eval $(call install-file-in, $(buildprefix)$(d)/nix-cmd.pc, $(libdir)/pkgconfig, 0644))

$(foreach i, $(wildcard $(d)/*.hh), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix, 0644)))
