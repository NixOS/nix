programs += nix-channel

nix-channel_DIR := $(d)

nix-channel_LIBS = libmain libformat libstore libutil

nix-channel_SOURCES := $(d)/nix-channel.cc
