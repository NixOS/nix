ifdef HOST_LINUX

  $(foreach n, nix-daemon.socket nix-daemon.service, $(eval $(call install-file-in, $(d)/$(n), $(prefix)/lib/systemd/system, 0644)))
  $(foreach n, nix-daemon.conf, $(eval $(call install-file-in, $(d)/$(n), $(prefix)/lib/tmpfiles.d, 0644)))

  clean-files += $(d)/nix-daemon.socket $(d)/nix-daemon.service $(d)/nix-daemon.conf

endif
