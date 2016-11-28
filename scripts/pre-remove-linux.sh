#!/bin/sh -e

# Stop and disable Nix worker

if command -v systemctl >/dev/null 2>&1; then
  systemctl stop nix-daemon.socket nix-daemon.service 2>/dev/null || true
  systemctl disable nix-daemon.socket nix-daemon.service 2>/dev/null || true
fi

if command -v service >/dev/null 2>&1; then
  service nix-daemon stop 2>/dev/null || true
fi
