# Nix exec build file
#============================================================================




# src files
#============================================================================

src_inc += include_directories('.')

nix_src = files(
    join_paths(meson.source_root(), 'src/nix/add-to-store.cc'),
    join_paths(meson.source_root(), 'src/nix/build.cc'),
    join_paths(meson.source_root(), 'src/nix/cat.cc'),
    join_paths(meson.source_root(), 'src/nix/command.cc'),
    join_paths(meson.source_root(), 'src/nix/copy.cc'),
    join_paths(meson.source_root(), 'src/nix/doctor.cc'),
    join_paths(meson.source_root(), 'src/nix/dump-path.cc'),
    join_paths(meson.source_root(), 'src/nix/edit.cc'),
    join_paths(meson.source_root(), 'src/nix/eval.cc'),
    join_paths(meson.source_root(), 'src/nix/hash.cc'),
    join_paths(meson.source_root(), 'src/nix/installables.cc'),
    join_paths(meson.source_root(), 'src/nix/legacy.cc'),
    join_paths(meson.source_root(), 'src/nix/log.cc'),
    join_paths(meson.source_root(), 'src/nix/ls.cc'),
    join_paths(meson.source_root(), 'src/nix/main.cc'),
    join_paths(meson.source_root(), 'src/nix/optimise-store.cc'),
    join_paths(meson.source_root(), 'src/nix/path-info.cc'),
    join_paths(meson.source_root(), 'src/nix/ping-store.cc'),
    join_paths(meson.source_root(), 'src/nix/progress-bar.cc'),
    join_paths(meson.source_root(), 'src/nix/repl.cc'),
    join_paths(meson.source_root(), 'src/nix/run.cc'),
    join_paths(meson.source_root(), 'src/nix/search.cc'),
    join_paths(meson.source_root(), 'src/nix/show-config.cc'),
    join_paths(meson.source_root(), 'src/nix/show-derivation.cc'),
    join_paths(meson.source_root(), 'src/nix/sigs.cc'),
    join_paths(meson.source_root(), 'src/nix/upgrade-nix.cc'),
    join_paths(meson.source_root(), 'src/nix/verify.cc'),
    join_paths(meson.source_root(), 'src/nix/why-depends.cc'),

    join_paths(meson.source_root(), 'src/build-remote/build-remote.cc'),
    join_paths(meson.source_root(), 'src/nix-build/nix-build.cc'),
    join_paths(meson.source_root(), 'src/nix-channel/nix-channel.cc'),
    join_paths(meson.source_root(), 'src/nix-collect-garbage/nix-collect-garbage.cc'),
    join_paths(meson.source_root(), 'src/nix-copy-closure/nix-copy-closure.cc'),
    join_paths(meson.source_root(), 'src/nix-daemon/nix-daemon.cc'),
    join_paths(meson.source_root(), 'src/nix-env/nix-env.cc'),
    join_paths(meson.source_root(), 'src/nix-env/user-env.cc'),
    join_paths(meson.source_root(), 'src/nix-instantiate/nix-instantiate.cc'),
    join_paths(meson.source_root(), 'src/nix-prefetch-url/nix-prefetch-url.cc'),
    join_paths(meson.source_root(), 'src/nix-store/dotgraph.cc'),
    join_paths(meson.source_root(), 'src/nix-store/graphml.cc'),
    join_paths(meson.source_root(), 'src/nix-store/nix-store.cc'))

nix_ln_src = [
    [ bindir, 'nix-build' ],
    [ bindir, 'nix-channel' ],
    [ bindir, 'nix-collect-garbage' ],
    [ bindir, 'nix-copy-closure' ],
    [ bindir, 'nix-daemon' ],
    [ bindir, 'nix-env' ],
    [ bindir, 'nix-hash' ],
    [ bindir, 'nix-instantiate' ],
    [ bindir, 'nix-prefetch-url' ],
    [ bindir, 'nix-shell' ],
    [ bindir, 'nix-store' ],
    [ join_paths(libexecdir, 'nix'), 'build-remote' ]]

nix_headers = files (
    join_paths(meson.source_root(), 'src/nix/command.hh'),
    join_paths(meson.source_root(), 'src/nix/legacy.hh'),
    join_paths(meson.source_root(), 'src/nix/progress-bar.hh'),
    join_paths(meson.source_root(), 'src/nix-env/user-env.hh'),
    join_paths(meson.source_root(), 'src/nix-store/dotgraph.hh'),
    join_paths(meson.source_root(), 'src/nix-store/graphml.hh'))

# dependancies
#============================================================================

nix_dep_list = [
    pthread_dep,
    libdl_dep,
    boost_dep,
    editline_dep,
    libsodium_dep]





# Link args
#============================================================================

nix_link_list = [
    libutil_lib,
    libstore_lib,
    libmain_lib,
    libexpr_lib
]

nix_link_args = []




# compiler args
#============================================================================

nix_cxx_args = []




# targets
#============================================================================




# build
#============================================================================

nix_bin = executable(
    'nix',
    install : true,
    install_mode : 'rwxr-xr-x',
    install_dir : bindir,
    include_directories : src_inc,
    sources : nix_src,
    link_with : nix_link_list,
    dependencies : nix_dep_list)


# meson does not yet support installing symlink files.
# this is a simple workaround
foreach name : nix_ln_src
    meson.add_install_script(
        'ln',
        '-sf',
        join_paths(bindir, 'nix'),
        name[1])
    meson.add_install_script(
        'cp',
        '-r',
        name[1],
        join_paths(name[0], name[1]))   
endforeach

install_headers(
    nix_headers,
    install_dir : join_paths(includedir, 'nix'))
