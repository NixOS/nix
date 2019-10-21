# Nix corepkgs build file
#============================================================================




# src files
#============================================================================


corepkgs_data = files(
    join_paths(meson.source_root(), 'corepkgs/buildenv.nix'),
    join_paths(meson.source_root(), 'corepkgs/derivation.nix'),
    join_paths(meson.source_root(), 'corepkgs/fetchurl.nix'),
    join_paths(meson.source_root(), 'corepkgs/imported-drv-to-derivation.nix'),
    join_paths(meson.source_root(), 'corepkgs/unpack-channel.nix'))





# targets
#============================================================================


config_nix = configuration_data()
config_nix.set('bash', bash.path())
config_nix.set('coreutils', coreutils)
config_nix.set('bzip2', bzip2.path())
config_nix.set('gzip', gzip.path())
config_nix.set('xz', xz.path())
config_nix.set('tar', tar.path())
config_nix.set('tarFlags', '')
config_nix.set('tr', tr.path())
config_nix.set('bindir', bindir)
config_nix.set('prefix', prefix)
config_nix.set('libexecdir', libexecdir)
config_nix.set('localstatedir', localstatedir)
config_nix.set('sysconfdir', sysconfdir)
config_nix.set('storedir', nixstoredir)





# build
#============================================================================


corepkgs_data += configure_file(
    input : 'config.nix.in',
    output : 'config.nix',
    configuration : config_nix)

install_data(
    corepkgs_data,
    install_mode : 'rwxr-xr-x',
    install_dir : join_paths(datadir, 'nix/corepkgs'))
