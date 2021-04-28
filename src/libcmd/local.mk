libraries += libcmd

libcmd_NAME = libnixcmd

libcmd_DIR := $(d)

libcmd_SOURCES := $(wildcard $(d)/*.cc)

libcmd_CXXFLAGS += -I src/libutil -I src/libstore -I src/libexpr -I src/libmain -I src/libfetchers -I src/nix

libcmd_LDFLAGS = $(EDITLINE_LIBS) -llowdown

libcmd_LIBS = libstore libutil libexpr libmain libfetchers libnix libwut

$(eval $(call install-file-in, $(d)/nix-cmd.pc, $(prefix)/lib/pkgconfig, 0644))
