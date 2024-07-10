project('nix', 'cpp',
  version : files('.version'),
  default_options : [
    'cpp_std=c++2a',
    # TODO(Qyriad): increase the warning level
    'warning_level=1',
    'debug=true',
    'optimization=2',
    'errorlogs=true', # Please print logs for tests that fail
  ],
  meson_version : '>= 1.1',
  license : 'LGPL-2.1-or-later',
)

cxx = meson.get_compiler('cpp')

subdir('build-utils-meson/deps-lists')

deps_private_maybe_subproject = [
  dependency('nix-util'),
  dependency('nix-store'),
  dependency('nix-expr'),
  dependency('nix-fetchers'),
  dependency('nix-main'),
  dependency('nix-cmd'),
]
deps_public_maybe_subproject = [
]
subdir('build-utils-meson/subprojects')

subdir('build-utils-meson/export-all-symbols')

add_project_arguments(
  # TODO(Qyriad): Yes this is how the autoconf+Make system did it.
  # It would be nice for our headers to be idempotent instead.
  '-include', 'config-util.hh',
  '-include', 'config-store.hh',
  '-include', 'config-expr.hh',
  #'-include', 'config-fetchers.hh',
  '-include', 'config-main.hh',
  '-include', 'config-cmd.hh',
  language : 'cpp',
)

subdir('build-utils-meson/diagnostics')
subdir('build-utils-meson/generate-header')

nix_sources = files(
  'add-to-store.cc',
  'app.cc',
  'build.cc',
  'bundle.cc',
  'cat.cc',
  'config-check.cc',
  'config.cc',
  'copy.cc',
  'derivation-add.cc',
  'derivation-show.cc',
  'derivation.cc',
  'develop.cc',
  'diff-closures.cc',
  'dump-path.cc',
  'edit.cc',
  'env.cc',
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
  'store-copy-log.cc',
  'store-delete.cc',
  'store-gc.cc',
  'store-info.cc',
  'store-repair.cc',
  'store.cc',
  'unix/daemon.cc',
  'upgrade-nix.cc',
  'verify.cc',
  'why-depends.cc',
)

nix_sources += [
  gen_header.process('doc/manual/generate-manpage.nix'),
  gen_header.process('doc/manual/generate-settings.nix'),
  gen_header.process('doc/manual/generate-store-info.nix'),
  gen_header.process('doc/manual/utils.nix'),
  gen_header.process('get-env.sh'),
  gen_header.process('profiles.md'),
  gen_header.process('help-stores.md'),
]

if host_machine.system() != 'windows'
  nix_sources += files(
    'unix/daemon.cc',
  )
endif

# The rest of the subdirectories aren't separate components,
# just source files in another directory, so we process them here.

build_remote_sources = files(
  'build-remote/build-remote.cc',
)
nix_build_sources = files(
  'nix-build/nix-build.cc',
)
nix_channel_sources = files(
  'nix-channel/nix-channel.cc',
)
unpack_channel_gen = gen_header.process('nix-channel/unpack-channel.nix')
nix_collect_garbage_sources = files(
  'nix-collect-garbage/nix-collect-garbage.cc',
)
nix_copy_closure_sources = files(
  'nix-copy-closure/nix-copy-closure.cc',
)
nix_env_buildenv_gen = gen_header.process('nix-env/buildenv.nix')
nix_env_sources = files(
  'nix-env/nix-env.cc',
  'nix-env/user-env.cc',
)
nix_instantiate_sources = files(
  'nix-instantiate/nix-instantiate.cc',
)
nix_store_sources = files(
  'nix-store/dotgraph.cc',
  'nix-store/graphml.cc',
  'nix-store/nix-store.cc',
)

# Hurray for Meson list flattening!
sources = [
  nix_sources,
  build_remote_sources,
  nix_build_sources,
  nix_channel_sources,
  unpack_channel_gen,
  nix_collect_garbage_sources,
  nix_copy_closure_sources,
  nix_env_buildenv_gen,
  nix_env_sources,
  nix_instantiate_sources,
  nix_store_sources,
]

include_dirs = [include_directories('.')]

this_exe = executable(
  meson.project_name(),
  sources,
  dependencies : deps_private_subproject + deps_private + deps_other,
  include_directories : include_dirs,
  link_args: linker_export_flags,
  install : true,
)
