ifeq ($(OS), Darwin)
  programs += resolve-system-dependencies
endif

resolve-system-dependencies_DIR := $(d)

resolve-system-dependencies_INSTALL_DIR := $(libexecdir)/nix

resolve-system-dependencies_LIBS := libstore libmain libutil

resolve-system-dependencies_SOURCES := $(d)/resolve-system-dependencies.cc
