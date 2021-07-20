# Nix binary build file
#============================================================================


nix_dir = meson.current_source_dir()
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
    libexpr_dep,
    libfetchers_dep,
    libcmd_dep]


# src files
#============================================================================

nix_src = files(
    join_paths(nix_dir, 'add-to-store.cc'),
    join_paths(nix_dir, 'app.cc'),
    join_paths(nix_dir, 'build.cc'),
    join_paths(nix_dir, 'bundle.cc'),
    join_paths(nix_dir, 'cat.cc'),
    join_paths(nix_dir, 'copy.cc'),
    join_paths(nix_dir, 'daemon.cc'),
    join_paths(nix_dir, 'describe-stores.cc'),
    join_paths(nix_dir, 'develop.cc'),
    join_paths(nix_dir, 'diff-closures.cc'),
    join_paths(nix_dir, 'doctor.cc'),
    join_paths(nix_dir, 'dump-path.cc'),
    join_paths(nix_dir, 'edit.cc'),
    join_paths(nix_dir, 'eval.cc'),
    join_paths(nix_dir, 'flake.cc'),
    join_paths(nix_dir, 'hash.cc'),
    join_paths(nix_dir, 'log.cc'),
    join_paths(nix_dir, 'ls.cc'),
    join_paths(nix_dir, 'main.cc'),
    join_paths(nix_dir, 'make-content-addressable.cc'),
    join_paths(nix_dir, 'nar.cc'),
    join_paths(nix_dir, 'optimise-store.cc'),
    join_paths(nix_dir, 'path-info.cc'),
    join_paths(nix_dir, 'ping-store.cc'),
    join_paths(nix_dir, 'prefetch.cc'),
    join_paths(nix_dir, 'profile.cc'),
    join_paths(nix_dir, 'registry.cc'),
    join_paths(nix_dir, 'repl.cc'),
    join_paths(nix_dir, 'run.cc'),
    join_paths(nix_dir, 'search.cc'),
    join_paths(nix_dir, 'show-config.cc'),
    join_paths(nix_dir, 'show-derivation.cc'),
    join_paths(nix_dir, 'sigs.cc'),
    join_paths(nix_dir, 'store.cc'),
    join_paths(nix_dir, 'store-delete.cc'),
    join_paths(nix_dir, 'store-gc.cc'),
    join_paths(nix_dir, 'store-repair.cc'),
    join_paths(nix_dir, 'upgrade-nix.cc'),
    join_paths(nix_dir, 'verify.cc'),
    join_paths(nix_dir, 'why-depends.cc'))

# nix_ln_src = [
#     [ bindir, 'nix-build' ],
#     [ bindir, 'nix-channel' ],
#     [ bindir, 'nix-collect-garbage' ],
#     [ bindir, 'nix-copy-closure' ],
#     [ bindir, 'nix-daemon' ],
#     [ bindir, 'nix-env' ],
#     [ bindir, 'nix-hash' ],
#     [ bindir, 'nix-instantiate' ],
#     [ bindir, 'nix-prefetch-url' ],
#     [ bindir, 'nix-shell' ],
#     [ bindir, 'nix-store' ],
#     [ join_paths(libexecdir, 'nix'), 'build-remote' ]]


# targets
#============================================================================
nix_src += custom_target(
  'get-env.sh.gen.hh',
  output : 'get-env.sh.gen.hh',
  input : 'get-env.sh',
  command : [bash, '-c', gen_rheader, 'sh', '@OUTPUT@'])


# build
#============================================================================

# set build args
#---------------------------------------------------
nix_cxx_args = []

nix_link_args = []

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
        nix_store_src],
    install : true,
    install_mode : 'rwxr-xr-x',
    install_dir : bindir,
    include_directories : proj_inc,
    cpp_args : nix_cxx_args,
    link_args : nix_link_args,
    dependencies : nix_dep_list)
