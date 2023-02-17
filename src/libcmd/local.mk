libraries += libcmd

libcmd_NAME = libnixcmd

libcmd_DIR := $(d)

libcmd_SOURCES := $(wildcard $(d)/*.cc)

libcmd_CXXFLAGS += \
	-Isrc/libutil/include \
	-Isrc/libstore \
	-Isrc/libexpr \
	-Isrc/libmain \
	-Isrc/libfetchers \
	-Isrc/nix

libcmd_LDFLAGS = $(EDITLINE_LIBS) $(LOWDOWN_LIBS) -pthread

libcmd_LIBS = libstore libutil libexpr libmain libfetchers

$(eval $(call install-file-in, $(d)/nix-cmd.pc, $(libdir)/pkgconfig, 0644))
