programs += nix-instantiate

nix-instantiate_DIR := $(d)
nix-instantiate_RELDIR := $(reldir)

nix-instantiate_SOURCES := nix-instantiate.cc

nix-instantiate_LIBS = libexpr libmain libstore libutil libformat
