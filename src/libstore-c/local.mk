libraries += libstorec

libstorec_NAME = libnixstorec

libstorec_DIR := $(d)

libstorec_SOURCES := $(wildcard $(d)/*.cc)

libstorec_LIBS = libutil libstore libutilc

libstorec_LDFLAGS += $(THREAD_LDFLAGS)

# Not just for this library itself, but also for downstream libraries using this library

INCLUDE_libstorec := -I $(d)
libstorec_CXXFLAGS += $(INCLUDE_libutil) $(INCLUDE_libutilc) \
		    $(INCLUDE_libstore) $(INCLUDE_libstorec)

$(eval $(call install-file-in, $(d)/nix-store-c.pc, $(libdir)/pkgconfig, 0644))

libstorec_FORCE_INSTALL := 1
