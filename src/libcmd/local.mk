libraries += libcmd

libcmd_NAME = libnixcmd

libcmd_DIR := $(d)

libcmd_SOURCES := $(wildcard $(d)/*.cc)

libcmd_CXXFLAGS += -I src/libutil -I src/libstore -I src/libexpr -I src/libmain -I src/libfetchers

libcmd_LDFLAGS = -llowdown

libcmd_LIBS = libstore libutil libexpr libmain libfetchers

$(eval $(call install-file-in, $(d)/nix-cmd.pc, $(prefix)/lib/pkgconfig, 0644))
