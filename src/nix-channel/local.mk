programs += nix-channel

nix-channel_DIR := $(d)

nix-channel_LIBS = libmain libutil libformat libstore

nix-channel_SOURCES := $(d)/nix-channel.cc
