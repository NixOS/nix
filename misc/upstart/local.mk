ifeq ($(initsystem), upstart)

  $(foreach n, nix-daemon.conf, $(eval $(call install-file-in, $(d)/$(n), $(sysconfdir)/init, 0644)))

endif
