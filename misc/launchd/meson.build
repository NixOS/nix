# nix launchd build file
#============================================================================

if (sys_name.contains('darwin'))

    launchd_data = []

    org_nixos_nix_dameon_plist = configuration_data()
    org_nixos_nix_dameon_plist.set('bindir', bindir)

    launchd_data += configure_file(
        input : 'org.nixos.nix-daemon.plist.in',
        output : 'org.nixos.nix-daemon.plist',
        configuration : org_nixos_nix_dameon_plist)

    install_data(
        corepkgs_data,
        install_dir : join_paths(prefix, 'Library/LaunchDaemons'))
endif
