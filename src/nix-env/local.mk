programs += nix-env

nix-env_DIR := $(d)

nix-env_SOURCES := $(wildcard $(d)/*.cc)

nix-env_LIBS = libexpr libmain libstore libutil libformat
