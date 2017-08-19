programs += nix-daemon

nix-daemon_DIR := $(d)
nix-daemon_RELDIR := $(reldir)

nix-daemon_SOURCES := nix-daemon.cc

nix-daemon_LIBS = libmain libstore libutil libformat

nix-daemon_LDFLAGS = -pthread

ifeq ($(OS), SunOS)
        nix-daemon_LDFLAGS += -lsocket
endif
