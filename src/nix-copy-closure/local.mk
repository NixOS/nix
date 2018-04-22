programs += nix-copy-closure

nix-copy-closure_DIR := $(d)

nix-copy-closure_LIBS = libmain libformat libstore libutil

nix-copy-closure_SOURCES := $(d)/nix-copy-closure.cc
