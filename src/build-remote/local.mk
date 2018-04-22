programs += build-remote

build-remote_DIR := $(d)

build-remote_INSTALL_DIR := $(libexecdir)/nix

build-remote_LIBS = libmain libformat libstore libutil

build-remote_SOURCES := $(d)/build-remote.cc
