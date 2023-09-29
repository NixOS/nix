libraries += libcmd

libcmd_NAME = libnixcmd

libcmd_DIR := $(d)

libcmd_SOURCES := $(wildcard $(d)/*.cc)

libcmd_CXXFLAGS += -I src/libutil -I src/libstore -I src/libfetchers -I src/libflake -I src/libexpr -I src/libmain

libcmd_LDFLAGS = $(EDITLINE_LIBS) $(LOWDOWN_LIBS) -pthread

libcmd_LIBS = libutil libstore libfetchers libflake libexpr libmain

$(eval $(call install-file-in, $(d)/nix-cmd.pc, $(libdir)/pkgconfig, 0644))
