programs += nix-copy-closure

nix-copy-closure_DIR := $(d)
nix-copy-closure_RELDIR := $(reldir)

nix-copy-closure_LIBS = libmain libutil libformat libstore

nix-copy-closure_SOURCES := nix-copy-closure.cc
