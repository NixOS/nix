libraries += libfindroots

libfindroots_NAME = libnixfindroots

libfindroots_DIR := $(d)/lib

libfindroots_SOURCES := $(wildcard $(d)/lib/*.cc)

programs += nix-find-roots

nix-find-roots_DIR := $(d)

nix-find-roots_SOURCES := $(d)/main.cc

nix-find-roots_LIBS := libfindroots

nix-find-roots_CXXFLAGS += \
	-I src/nix-find-roots/lib

nix-find-roots_INSTALL_DIR := $(libexecdir)/nix
