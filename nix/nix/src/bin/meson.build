# Nix binary build file
#============================================================================


nix_inc = [include_directories('.')]

# dependancies
#============================================================================

nix_dep_list = [
    pthread_dep,
    libdl_dep,
    boost_dep,
    editline_dep,
    libsodium_dep,
    gc_dep,

    libutil_dep,
    libstore_dep,
    libmain_dep,
    libfetchers_dep,
    libexpr_dep]


# src files
#============================================================================

nix_src = files(
    'add-to-store.cc',
    'app.cc',
    'build.cc',
    'bundle.cc',
    'cat.cc',
    'copy.cc',
    'daemon.cc',
    'develop.cc',
    'diff-closures.cc',
    'doctor.cc',
    'dump-path.cc',
    'edit.cc',
    'eval.cc',
    'flake.cc',
    'fmt.cc',
    'hash.cc',
    'log.cc',
    'ls.cc',
    'main.cc',
    'make-content-addressed.cc',
    'nar.cc',
    'optimise-store.cc',
    'path-from-hash-part.cc',
    'path-info.cc',
    'ping-store.cc',
    'prefetch.cc',
    'profile.cc',
    'realisation.cc',
    'registry.cc',
    'run.cc',
    'search.cc',
    'show-config.cc',
    'sigs.cc',
    'store.cc',
    'store-copy-log.cc',
    'store-delete.cc',
    'store-gc.cc',
    'store-repair.cc',
    'upgrade-nix.cc',
    'verify.cc',
    'why-depends.cc')

nix_headers = files(
    'run.hh')


# build
#============================================================================

# set build args
#---------------------------------------------------
nix_cxx_args = []
libcmd_cxx_args = []

nix_link_args = []
libcmd_link_args = []

# build library
#---------------------------------------------------
libcmd_lib = library(
    'nixcmd',
    sources : [
        libcmd_src],
    install : true,
    install_mode : 'rwxr-xr-x',
    install_dir : libdir,
    include_directories : [
        libcmd_inc,
        proj_inc],
    cpp_args : libcmd_cxx_args,
    link_args : libcmd_link_args,
    dependencies : libcmd_dep_list)

# install headers
#---------------------------------------------------
install_headers(
    libcmd_headers,
    install_dir : join_paths(includedir, 'nix'))

# declare dependency
#---------------------------------------------------
libcmd_dep = declare_dependency(
    link_with : libcmd_lib,
    include_directories : libcmd_inc)

nix_dep_list += libcmd_dep


# build executable
#---------------------------------------------------
nix_bin = executable(
    'nix',
    sources : [
        nix_src,
        build_remote_src,
        nix_build_src,
        nix_channel_src,
        nix_collect_garbage_src,
        nix_copy_closure_src,
        # nix_daemon_src,
        nix_env_src,
        nix_instantiate_src,
        nix_store_src,
        utils_nix,
        generate_manpage_nix],
    install : true,
    install_mode : 'rwxr-xr-x',
    install_dir : bindir,
    include_directories : [
        doc_manual_inc,
        proj_inc],
    cpp_args : nix_cxx_args,
    link_args : nix_link_args,
    dependencies : nix_dep_list)

# Install the symlinks for the nix binary
nix_symlink_list = [
    'nix-build',
    'nix-channel',
    'nix-collect-garbage',
    'nix-copy-closure',
    'nix-daemon',
    'nix-env',
    'nix-hash',
    'nix-instantiate',
    'nix-prefetch-url',
    'nix-shell',
    'nix-store']

foreach nix_symlink : nix_symlink_list
    install_symlink(
        nix_symlink,
        install_dir : bindir,
        pointing_to : 'nix')
endforeach
