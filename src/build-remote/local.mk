programs += build-remote

build-remote_DIR := $(d)

build-remote_INSTALL_DIR := $(libexecdir)/nix

build-remote_LIBS = libmain libutil libformat libstore

build-remote_SOURCES := $(d)/build-remote.cc

build-remote_CXXFLAGS = -DSYSCONFDIR="\"$(sysconfdir)\"" -Isrc/nix-store
