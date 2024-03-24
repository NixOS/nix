

nix_data_inc = include_directories('.')



nix_profile_md = files(
    'profile.md')

nix_index_md_in = files(
    join_paths('store', 'types', 'index.md.in'))

nix_md = [
    nix_profile_md,
    nix_index_md_in]

nix_md += files(
    'add.md',
    'build.md',
    'bundle.md',
    'copy.md',
    'daemon.md',
    'derivation-add.md',
    'derivation-show.md',
    'develop.md',
    'diff-closures.md',
    'edit.md',
    'eval.md',
    'flake-archive.md',
    'flake-check.md',
    'flake-clone.md',
    'flake-init.md',
    'flake-lock.md',
    'flake.md',
    'flake-metadata.md',
    'flake-new.md',
    'flake-prefetch.md',
    'flake-show.md',
    'flake-update.md',
    'fmt.md',
    'hash-convert.md',
    'help.md',
    'key-convert-secret-to-public.md',
    'key-generate-secret.md',
    'log.md',
    'make-content-addressed.md',
    'meson.build',
    'nar-cat.md',
    'nar-dump-path.md',
    'nar-ls.md',
    'nar.md',
    'nix.md',
    'optimise-store.md',
    'path-from-hash-part.md',
    'path-info.md',
    'print-dev-env.md',
    'profile-diff-closures.md',
    'profile-history.md',
    'profile-install.md',
    'profile-list.md',
    'profile.md',
    'profile-remove.md',
    'profile-rollback.md',
    'profile-upgrade.md',
    'profile-wipe-history.md',
    'registry-add.md',
    'registry-list.md',
    'registry.md',
    'registry-pin.md',
    'registry-remove.md',
    'repl.md',
    'run.md',
    'search.md',
    'shell.md',
    'store-cat.md',
    'store-copy-log.md',
    'store-delete.md',
    'store-dump-path.md',
    'store-gc.md',
    'store-info.md',
    'store-ls.md',
    'store-prefetch-file.md',
    'store-repair.md',
    'upgrade-nix.md',
    'verify.md',
    'why-depends.md')


nix_data_dirs = [
    'realisation']




foreach dir : nix_data_dirs
    subdir(dir)
endforeach
