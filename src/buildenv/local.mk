programs += buildenv

buildenv_DIR := $(d)

buildenv_INSTALL_DIR := $(libexecdir)/nix

buildenv_LIBS = libmain libutil libformat

buildenv_SOURCES := $(d)/buildenv.cc
