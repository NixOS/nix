programs += nix-collect-garbage

nix-collect-garbage_DIR := $(d)
nix-collect-garbage_RELDIR := $(reldir)

nix-collect-garbage_SOURCES := nix-collect-garbage.cc

nix-collect-garbage_LIBS = libmain libstore libutil libformat
