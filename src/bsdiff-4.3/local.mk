programs += bsdiff bspatch

bsdiff_DIR := $(d)
bsdiff_SOURCES := $(d)/bsdiff.c
bsdiff_LDFLAGS = -lbz2 $(bsddiff_compat_include)
bsdiff_INSTALL_DIR = $(libexecdir)/nix

bspatch_DIR := $(d)
bspatch_SOURCES := $(d)/bspatch.c
bspatch_LDFLAGS = -lbz2 $(bsddiff_compat_include)
bspatch_INSTALL_DIR = $(libexecdir)/nix
