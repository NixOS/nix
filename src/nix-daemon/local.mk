programs += nix-daemon

nix-daemon_DIR := $(d)

nix-daemon_SOURCES := $(d)/nix-daemon.cc

nix-daemon_LIBS = libmain libstore libutil libformat

nix-daemon_LDFLAGS = -pthread

ifeq ($(OS), SunOS)
        nix-daemon_LDFLAGS += -lsocket
endif
