programs += nix-env

nix-env_DIR := $(d)
nix-env_RELDIR := $(reldir)

nix-env_SOURCES := $(subst $(d)/,,$(wildcard $(d)/*.cc))

nix-env_LIBS = libexpr libmain libstore libutil libformat
