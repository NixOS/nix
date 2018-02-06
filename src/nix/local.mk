programs += nix

nix_DIR := $(d)

nix_SOURCES := $(wildcard $(d)/*.cc) $(wildcard src/linenoise/*.cpp)

nix_LIBS = libexpr libmain libstore libutil libformat

nix_LDFLAGS = -pthread

$(eval $(call install-symlink, nix, $(bindir)/nix-hash))
