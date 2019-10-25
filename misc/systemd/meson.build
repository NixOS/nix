# nix systemd build file
#============================================================================

if (sys_name.contains('linux'))

    systemd_data = []

    systemd_nix_daemon = configuration_data()
    systemd_nix_daemon.set('bindir', bindir)
    systemd_nix_daemon.set('storedir', nixstoredir)
    systemd_nix_daemon.set('localstatedir', localstatedir)

    systemd_data += configure_file(
        input : 'nix-daemon.socket.in',
        output : 'nix-daemon.socket',
        configuration : systemd_nix_daemon)

    systemd_data += configure_file(
        input : 'nix-daemon.service.in',
        output : 'nix-daemon.service',
        configuration : systemd_nix_daemon)

    install_data(
        systemd_data,
        install_dir : join_paths(prefix, 'lib/systemd/system'))
endif
