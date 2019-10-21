# Nix corepkgs build file
#============================================================================




# src files
#============================================================================

scripts_data = files(
    join_paths(meson.source_root(), 'scripts/install-darwin-multi-user.sh'),
    join_paths(meson.source_root(), 'scripts/install-multi-user.sh'))





# targets
#============================================================================

nix_profile_sh = configuration_data()
nix_profile_sh.set('localstatedir', localstatedir)
nix_profile_sh.set('coreutils', coreutils)

nix_http_export_cgi = configuration_data()
nix_http_export_cgi.set('bindir', bindir)
nix_http_export_cgi.set('localstatedir', localstatedir)
nix_http_export_cgi.set('coreutils', coreutils)
nix_http_export_cgi.set('gzip', gzip.path())

nix_reduce_build = configuration_data()
nix_reduce_build.set('bash', bash.path())
nix_reduce_build.set('bindir', bindir)
nix_reduce_build.set('localstatedir', localstatedir)


# TODO: make these work
nix_install = configuration_data()
nix_install.set('nixVersion', package_version)
nix_install.set('binaryTarball_x86_64-linux', '')
nix_install.set('binaryTarball_i686-linux', '')
nix_install.set('binaryTarball_aarch64-linux', '')
nix_install.set('binaryTarball_x86_64-darwin', '')




# build
#============================================================================

scripts_data += configure_file(
    input : 'nix-profile.sh.in',
    output : 'nix.sh',
    configuration : nix_profile_sh)

scripts_data += configure_file(
    input : 'nix-profile-daemon.sh.in',
    output : 'nix-daemon.sh',
    configuration : nix_profile_sh)

scripts_data += configure_file(
    input : 'nix-http-export.cgi.in',
    output : 'nix-http-export.cgi',
    configuration : nix_http_export_cgi)

scripts_data += configure_file(
    input : 'nix-reduce-build.in',
    output : 'nix-reduce-build',
    configuration : nix_reduce_build)

install_data(
    scripts_data,
    install_mode : 'rwxr-xr-x',
    install_dir : profiledir)
