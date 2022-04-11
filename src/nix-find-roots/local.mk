ifndef HOST_DARWIN
libraries += find-roots

find-roots_NAME = libfindroots

find-roots_DIR := $(d)/lib

find-roots_SOURCES := $(wildcard $(d)/lib/*.cc)

programs += nix-find-roots

nix-find-roots_DIR := $(d)

nix-find-roots_SOURCES := $(d)/main.cc

nix-find-roots_LIBS := find-roots

nix-find-roots_CXXFLAGS += \
	-I src/nix-find-roots/lib

nix-find-roots_INSTALL_DIR := $(libexecdir)/nix
endif
