libraries += libcmd

libcmd_NAME = libnixcmd

libcmd_DIR := $(d)

libcmd_SOURCES := $(wildcard $(d)/*.cc)

libcmd_LIBS = libstore libutil libexpr libmain

libcmd_LDFLAGS = -pthread

$(eval $(call install-file-in, $(d)/nix-cmd.pc, $(prefix)/lib/pkgconfig, 0644))
