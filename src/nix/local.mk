programs += nix

nix_DIR := $(d)

nix_SOURCES := $(wildcard $(d)/*.cc)

nix_LIBS = libexpr libmain libstore libutil libformat

ifeq ($(HAVE_READLINE), 1)
  nix_LDFLAGS += -lreadline
endif

$(eval $(call install-symlink, nix, $(bindir)/nix-hash))
