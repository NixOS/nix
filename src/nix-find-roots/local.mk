programs += nix-find-roots

nix-find-roots_DIR := $(d)

nix-find-roots_SOURCES := $(wildcard $(d)/*.cc)

nix-find-roots_INSTALL_DIR := $(libexecdir)/nix

ifdef HOST_DARWIN
	nix-find-roots_LDFLAGS = -lc++fs
endif
