libraries += libmain

libmain_NAME = libnixmain

libmain_DIR := $(d)
libmain_RELDIR := $(reldir)

libmain_SOURCES := $(subst $(d)/,,$(wildcard $(d)/*.cc))

libmain_LDFLAGS = $(OPENSSL_LIBS)

libmain_LIBS = libstore libutil libformat

libmain_ALLOW_UNDEFINED = 1

$(eval $(call install-file-in, $(d)/nix-main.pc, $(prefix)/lib/pkgconfig, 0644))
