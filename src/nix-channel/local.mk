programs += nix-channel

nix-channel_DIR := $(d)
nix-channel_RELDIR := $(reldir)

nix-channel_LIBS = libmain libutil libformat libstore

nix-channel_SOURCES := nix-channel.cc
