# Nix corepkgs build file
#============================================================================

scripts_dir = meson.current_source_dir()
scripts_inc = [include_directories('.')]


# src files
#============================================================================

scripts_data = files(
    'install-darwin-multi-user.sh',
    'install-multi-user.sh')


# targets
#============================================================================

# sh nix profile
#---------------------------------------------------
nix_profile_sh = configuration_data()
nix_profile_sh.set('localstatedir', localstatedir)
nix_profile_sh.set('coreutils', coreutils)

# fish nix profile
#---------------------------------------------------
nix_profile_fish = configuration_data()
nix_profile_fish.set('localstatedir', localstatedir)
nix_profile_fish.set('coreutils', coreutils)

nix_http_export_cgi = configuration_data()
nix_http_export_cgi.set('bindir', bindir)
nix_http_export_cgi.set('localstatedir', localstatedir)
nix_http_export_cgi.set('coreutils', coreutils)
nix_http_export_cgi.set('gzip', gzip.full_path())

nix_reduce_build = configuration_data()
nix_reduce_build.set('bash', bash.full_path())
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

# configure sh scripts
#---------------------------------------------------
scripts_data += configure_file(
    input : 'nix-profile.sh.in',
    output : 'nix.sh',
    configuration : nix_profile_sh)

scripts_data += configure_file(
    input : 'nix-profile-daemon.sh.in',
    output : 'nix-daemon.sh',
    configuration : nix_profile_sh)

# configure fish scripts
#---------------------------------------------------
scripts_data += configure_file(
    input : 'nix-profile.fish.in',
    output : 'nix-profile.fish',
    configuration : nix_profile_sh)

scripts_data += configure_file(
    input : 'nix-profile-daemon.fish.in',
    output : 'nix-daemon.fish',
    configuration : nix_profile_fish)



# scripts_data += configure_file(
#     input : 'nix-http-export.cgi.in',
#     output : 'nix-http-export.cgi',
#     configuration : nix_http_export_cgi)

# scripts_data += configure_file(
#     input : 'nix-reduce-build.in',
#     output : 'nix-reduce-build',
#     configuration : nix_reduce_build)

# install scripts
#---------------------------------------------------
install_data(
    scripts_data,
    install_mode : 'rwxr-xr-x',
    install_dir : profiledir)
