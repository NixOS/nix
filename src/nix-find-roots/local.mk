ifndef HOST_DARWIN
programs += nix-find-roots

nix-find-roots_DIR := $(d)

nix-find-roots_SOURCES := $(wildcard $(d)/*.cc)

nix-find-roots_INSTALL_DIR := $(libexecdir)/nix
endif
