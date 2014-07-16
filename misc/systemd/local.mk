ifeq ($(OS), Linux)

  $(foreach n, nix-daemon.socket nix-daemon.service, $(eval $(call install-file-in, $(d)/$(n), $(prefix)/lib/systemd/system, 0644)))

endif
