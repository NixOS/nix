programs += nix-pack

nix-pack_DIR := $(d)

nix-pack_SOURCES := $(d)/nix-pack.cc

nix-pack_LIBS = libexpr libmain libstore libutil libformat
