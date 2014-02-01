programs += nix-daemon

nix-daemon_DIR := $(d)

nix-daemon_SOURCES := $(d)/nix-daemon.cc

nix-daemon_LIBS = libmain libstore libutil libformat

$(eval $(call install-symlink, nix-daemon, $(bindir)/nix-worker))
