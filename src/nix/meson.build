project('nix', 'cpp',
  version : files('.version'),
  default_options : [
    'cpp_std=c++2a',
    # TODO(Qyriad): increase the warning level
    'warning_level=1',
    'errorlogs=true', # Please print logs for tests that fail
    'localstatedir=/nix/var',
  ],
  meson_version : '>= 1.1',
  license : 'LGPL-2.1-or-later',
)

cxx = meson.get_compiler('cpp')

subdir('nix-meson-build-support/deps-lists')

nix_store = dependency('nix-store')

deps_private_maybe_subproject = [
  dependency('nix-util'),
  nix_store,
  dependency('nix-expr'),
  dependency('nix-flake'),
  dependency('nix-fetchers'),
  dependency('nix-main'),
  dependency('nix-cmd'),
]
deps_public_maybe_subproject = [
]
subdir('nix-meson-build-support/subprojects')

subdir('nix-meson-build-support/export-all-symbols')
subdir('nix-meson-build-support/windows-version')

configdata = configuration_data()

# The CLI has a more detailed version string than the libraries; see `nixVersion`
configdata.set_quoted('NIX_CLI_VERSION', meson.project_version())

fs = import('fs')

bindir = get_option('bindir')
if not fs.is_absolute(bindir)
  bindir = get_option('prefix') / bindir
endif
configdata.set_quoted('NIX_BIN_DIR', bindir)

config_h = configure_file(
  configuration : configdata,
  output : 'config-nix-cli.hh',
)

add_project_arguments(
  # TODO(Qyriad): Yes this is how the autoconf+Make system did it.
  # It would be nice for our headers to be idempotent instead.
  '-include', 'config-util.hh',
  '-include', 'config-store.hh',
  '-include', 'config-expr.hh',
  #'-include', 'config-fetchers.hh',
  '-include', 'config-main.hh',
  '-include', 'config-cmd.hh',
  '-include', 'config-nix-cli.hh',
  language : 'cpp',
)

subdir('nix-meson-build-support/common')
subdir('nix-meson-build-support/generate-header')

nix_sources = [config_h] + files(
  'add-to-store.cc',
  'app.cc',
  'self-exe.cc',
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
  'man-pages.cc',
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
  'upgrade-nix.cc',
  'verify.cc',
  'why-depends.cc',
)

if host_machine.system() != 'windows'
  nix_sources += files(
    'unix/daemon.cc',
  )
endif

nix_sources += [
  gen_header.process('doc/manual/generate-manpage.nix'),
  gen_header.process('doc/manual/generate-settings.nix'),
  gen_header.process('doc/manual/generate-store-info.nix'),
  gen_header.process('doc/manual/utils.nix'),
  gen_header.process('get-env.sh'),
  gen_header.process('profiles.md'),
  gen_header.process('help-stores.md'),
]

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
  nix_build_sources,
  unpack_channel_gen,
  nix_collect_garbage_sources,
  nix_copy_closure_sources,
  nix_env_buildenv_gen,
  nix_env_sources,
  nix_instantiate_sources,
  nix_store_sources,
]

if host_machine.system() != 'windows'
  sources += [
    build_remote_sources,
    nix_channel_sources,
  ]
endif

fs = import('fs')
prefix = get_option('prefix')

mandir = get_option('mandir')
mandir = fs.is_absolute(mandir) ? mandir : prefix / mandir

cpp_args= [
  '-DNIX_MAN_DIR="@0@"'.format(mandir)
]

include_dirs = [include_directories('.')]

this_exe = executable(
  meson.project_name(),
  sources,
  dependencies : deps_private_subproject + deps_private + deps_other,
  include_directories : include_dirs,
  cpp_args : cpp_args,
  link_args: linker_export_flags,
  install : true,
)

meson.override_find_program('nix', this_exe)

nix_symlinks = [
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
  'nix-store',
]

executable_suffix = ''
if host_machine.system() == 'windows'
  executable_suffix = '.exe'
endif

foreach linkname : nix_symlinks
  install_symlink(
    linkname + executable_suffix,
    # TODO(Qyriad): should these continue to be relative symlinks?
    pointing_to : fs.name(this_exe),
    install_dir : get_option('bindir'),
    # The 'runtime' tag is what executables default to, which we want to emulate here.
    install_tag : 'runtime'
  )
  custom_target(
    command: ['ln', '-sf', fs.name(this_exe), '@OUTPUT@'],
    output: linkname + executable_suffix,
    # native doesn't allow dangling symlinks, but the target executable often doesn't exist at this time
    env : { 'MSYS' : 'winsymlinks:lnk' },
    # TODO(Ericson2314): Don't do this once we have the `meson.override_find_program` working)
    build_by_default: true
  )
  # TODO(Ericson3214): Dosen't yet work
  #meson.override_find_program(linkname, t)
endforeach

install_symlink(
  'build-remote',
  pointing_to : '..' / '..'/ get_option('bindir') / fs.name(this_exe),
  install_dir : get_option('libexecdir') / fs.name(this_exe),
  # The 'runtime' tag is what executables default to, which we want to emulate here.
  install_tag : 'runtime'
)

custom_target(
  command: ['ln', '-sf', fs.name(this_exe), '@OUTPUT@'],
  output: 'build-remote' + executable_suffix,
  # native doesn't allow dangling symlinks, but the target executable often doesn't exist at this time
  env : { 'MSYS' : 'winsymlinks:lnk' },
  # TODO(Ericson2314): Don't do this once we have the `meson.override_find_program` working)
  build_by_default: true
)
# TODO(Ericson3214): Dosen't yet work
#meson.override_find_program(linkname, t)

localstatedir = nix_store.get_variable(
  'localstatedir',
  default_value : get_option('localstatedir'),
)
assert(localstatedir == get_option('localstatedir'))
store_dir = nix_store.get_variable('storedir')
subdir('scripts')
subdir('misc')
