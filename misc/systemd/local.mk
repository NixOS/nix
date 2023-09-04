ifdef HOST_LINUX

  $(foreach n, nix-daemon.socket nix-daemon.service, $(eval $(call install-file-in, $(d)/$(n), $(prefix)/lib/systemd/system, 0644)))

  clean-files += $(d)/nix-daemon.socket $(d)/nix-daemon.service

endif
