programs += download-via-ssh

download-via-ssh_DIR := $(d)

download-via-ssh_SOURCES := $(d)/download-via-ssh.cc

download-via-ssh_INSTALL_DIR := $(libexecdir)/nix/substituters

download-via-ssh_CXXFLAGS = -Isrc/nix-store

download-via-ssh_LIBS = libmain libstore libutil libformat
