libraries += libstorec

libstorec_NAME = libnixstorec

libstorec_DIR := $(d)

libstorec_SOURCES := $(wildcard $(d)/*.cc)

libstorec_LIBS = libutil libstore libutilc

libstorec_LDFLAGS += -pthread

libstorec_CXXFLAGS += -I src/libutil -I src/libstore -I src/libutil/c

$(eval $(call install-file-in, $(d)/nix-store-c.pc, $(libdir)/pkgconfig, 0644))

libstorec_FORCE_INSTALL := 1
