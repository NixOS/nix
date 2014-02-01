programs += nix-hash

nix-hash_DIR := $(d)

nix-hash_SOURCES := $(d)/nix-hash.cc

nix-hash_LIBS = libmain libstore libutil libformat
