programs += nix-prefetch-url

nix-prefetch-url_DIR := $(d)
nix-prefetch-url_RELDIR := $(reldir)

nix-prefetch-url_SOURCES := nix-prefetch-url.cc

nix-prefetch-url_LIBS = libmain libexpr libstore libutil libformat
