ifeq ($(OS), Darwin)
  programs += resolve-system-dependencies
endif

resolve-system-dependencies_DIR := $(d)
resolve-system-dependencies_RELDIR := $(reldir)

resolve-system-dependencies_INSTALL_DIR := $(libexecdir)/nix

resolve-system-dependencies_LIBS := libstore libmain libutil libformat

resolve-system-dependencies_SOURCES := resolve-system-dependencies.cc
