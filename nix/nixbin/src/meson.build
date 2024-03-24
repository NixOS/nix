# nix build file
#============================================================================

nix_inc += include_directories('.')

nix_src_dirs = [
    'build-remote',
    'nix-build',
    'nix-channel',
    'nix-collect-garbage',
    'nix-copy-closure',
    'nix-env',
    'nix-instantiate',
    'nix-store',
    'bin',
]

if sys_name == 'Darwin'
    nix_src_dirs += 'resolve-system-dependencies'
endif

foreach dir : nix_src_dirs
    subdir(dir)
endforeach


# build
#============================================================================

# build executable
#---------------------------------------------------
nix_bin = executable(
    'nix',
    sources : [
        get_env_hh,
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
        # utils_nix,
        # generate_manpage_nix
        ],
    install : true,
    install_mode : 'rwxr-xr-x',
    install_dir : bindir,
    include_directories : [
        nix_inc,
        # doc_manual_inc,
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
