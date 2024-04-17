libraries += libmain

libmain_NAME = libnixmain

libmain_DIR := $(d)

libmain_SOURCES := $(wildcard $(d)/*.cc)
ifdef HOST_UNIX
  libmain_SOURCES += $(wildcard $(d)/unix/*.cc)
endif

INCLUDE_libmain := -I $(d)

libmain_CXXFLAGS += $(INCLUDE_libutil) $(INCLUDE_libstore) $(INCLUDE_libmain)

libmain_LDFLAGS += $(OPENSSL_LIBS)

libmain_LIBS = libstore libutil

libmain_ALLOW_UNDEFINED = 1

$(eval $(call install-file-in, $(buildprefix)$(d)/nix-main.pc, $(libdir)/pkgconfig, 0644))
