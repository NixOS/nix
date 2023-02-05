libraries += libstore-cmd

libstore-cmd_NAME = libnixstore-cmd

libstore-cmd_DIR := $(d)

libstore-cmd_SOURCES := $(wildcard $(d)/*.cc)

libstore-cmd_CXXFLAGS += -I src/libutil -I src/libstore -I src/libmain

libstore-cmd_LDFLAGS += $(LOWDOWN_LIBS) -pthread

libstore-cmd_LIBS = libstore libutil libmain

$(eval $(call install-file-in, $(d)/nix-store-cmd.pc, $(libdir)/pkgconfig, 0644))

$(foreach i, $(wildcard $(d)/*.hh), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix, 0644)))
