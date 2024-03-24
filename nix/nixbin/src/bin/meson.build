# Nix binary build file
#============================================================================




# dependencies
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
    libexpr_dep,
    libcmd_dep]


# src files
#============================================================================

nix_src = files(
    'add-to-store.cc',
    'app.cc',
    'build.cc',
    'bundle.cc',
    'cat.cc',
    'config.cc',
    'config-check.cc',
    'copy.cc',
    'daemon.cc',
    'derivation-add.cc',
    'derivation.cc',
    'derivation-show.cc',
    'develop.cc',
    'diff-closures.cc',
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
    'prefetch.cc',
    'profile.cc',
    'realisation.cc',
    'registry.cc',
    'repl.cc',
    'run.cc',
    'search.cc',
    'sigs.cc',
    'store.cc',
    'store-copy-log.cc',
    'store-delete.cc',
    'store-gc.cc',
    'store-info.cc',
    'store-repair.cc',
    'upgrade-nix.cc',
    'verify.cc',
    'why-depends.cc')

nix_headers = files(
    'run.hh')

nix_markdown_files += files(
    join_paths('realisation', 'info.md'))
