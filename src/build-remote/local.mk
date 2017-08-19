programs += build-remote

build-remote_DIR := $(d)
build-remote_RELDIR := $(reldir)

build-remote_INSTALL_DIR := $(libexecdir)/nix

build-remote_LIBS = libmain libutil libformat libstore

build-remote_SOURCES := build-remote.cc
