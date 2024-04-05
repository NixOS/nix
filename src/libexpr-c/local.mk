libraries += libexprc

libexprc_NAME = libnixexprc

libexprc_DIR := $(d)

libexprc_SOURCES := \
  $(wildcard $(d)/*.cc) \

# Not just for this library itself, but also for downstream libraries using this library

INCLUDE_libexprc := -I $(d)
libexprc_CXXFLAGS += $(INCLUDE_libutil) $(INCLUDE_libutilc) \
                     $(INCLUDE_libfetchers) \
                     $(INCLUDE_libstore) $(INCLUDE_libstorec) \
                     $(INCLUDE_libexpr) $(INCLUDE_libexprc)

libexprc_LIBS = libutil libutilc libstore libstorec libexpr

libexprc_LDFLAGS += $(THREAD_LDFLAGS)

$(eval $(call install-file-in, $(d)/nix-expr-c.pc, $(libdir)/pkgconfig, 0644))

libexprc_FORCE_INSTALL := 1

