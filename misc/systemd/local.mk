$(foreach n, nix-daemon.socket nix-daemon.service, $(eval $(call install-file-in, $(d)/$(n), $(libdir)/systemd/system, 0644)))
