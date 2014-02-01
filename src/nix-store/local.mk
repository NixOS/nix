programs += nix-store

nix-store_DIR := $(d)

nix-store_SOURCES := $(wildcard $(d)/*.cc)

nix-store_LIBS = libmain libstore libutil libformat

nix-store_LDFLAGS = -lbz2
