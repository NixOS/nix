# ifeq ($(OS), Linux)

#   $(foreach n, nix-daemon.conf, $(eval $(call install-file-in, $(d)/$(n), $(sysconfdir)/init, 0644)))

# endif


# nix systemd build file
#============================================================================

if (sys_name.contains('linux'))

    upstart_data = []

    upstart_nix_daemon = configuration_data()
    upstart_nix_daemon.set('bindir', bindir)

    upstart_data += configure_file(
        input : 'nix-daemon.conf.in',
        output : 'nix-daemon.conf',
        configuration : upstart_nix_daemon)

    install_data(
        upstart_data,
        install_dir : join_paths(sysconfdir, 'init'))
endif
