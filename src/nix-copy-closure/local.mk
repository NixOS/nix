programs += nix-copy-closure

nix-copy-closure_DIR := $(d)

nix-copy-closure_LIBS = libmain libutil libformat libstore

nix-copy-closure_SOURCES := $(d)/nix-copy-closure.cc
