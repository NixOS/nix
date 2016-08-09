programs += nix-build

nix-build_DIR := $(d)

nix-build_SOURCES := $(d)/nix-build.cc

nix-build_LIBS = libmain libstore libutil libformat

$(eval $(call install-symlink, nix-build, $(bindir)/nix-shell))
