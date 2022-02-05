libraries += libcmd

libcmd_NAME = libnixcmd

libcmd_DIR := $(d)

libcmd_SOURCES := $(wildcard $(d)/*.cc)

libcmd_CXXFLAGS += -I src/libutil -I src/libstore -I src/libexpr -I src/libmain -I src/libfetchers -I src/nix

# libcmd_LDFLAGS += -llowdown -pthread
libcmd_LDFLAGS = $(EDITLINE_LIBS) -llowdown -pthread
# libcmd_LDFLAGS += $(LOWDOWN_LIBS) -pthread

libcmd_LIBS = libstore libutil libexpr libmain libfetchers libnix

$(eval $(call install-file-in, $(d)/nix-cmd.pc, $(libdir)/pkgconfig, 0644))
