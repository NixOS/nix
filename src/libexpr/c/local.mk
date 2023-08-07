libraries += libexprc

libexprc_NAME = libnixexprc

libexprc_DIR := $(d)

libexprc_SOURCES := \
  $(wildcard $(d)/*.cc) \

libexprc_CXXFLAGS += -I src/libutil -Isrc/libfetchers -I src/libstore -I src/libstorec -I src/libexpr -I src/libutil/c -I src/libstore/c

libexprc_LIBS = libutil libutilc libstorec libexpr

libexprc_LDFLAGS += -pthread

$(eval $(call install-file-in, $(d)/nix-expr-c.pc, $(libdir)/pkgconfig, 0644))

libexprc_FORCE_INSTALL := 1

