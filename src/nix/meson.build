project(
  'nix',
  'cpp',
  version : files('.version'),
  default_options : [
    'cpp_std=c++23',
    # TODO(Qyriad): increase the warning level
    'warning_level=1',
    'errorlogs=true', # Please print logs for tests that fail
  ],
  meson_version : '>= 1.4',
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
deps_public_maybe_subproject = []
subdir('nix-meson-build-support/subprojects')

link_args = []
link_whole = []

if get_option('plugin-c-api')
  # TODO: Is there a better way to handle this?

  c_api_libs = {
    'nix-util-c' : {
      'subproj' : 'libutil-c',
      'archive' : 'libnixutilc.a',
    },
    'nix-store-c' : {
      'subproj' : 'libstore-c',
      'archive' : 'libnixstorec.a',
    },
    'nix-fetchers-c' : {
      'subproj' : 'libfetchers-c',
      'archive' : 'libnixfetchersc.a',
    },
    'nix-expr-c' : {
      'subproj' : 'libexpr-c',
      'archive' : 'libnixexprc.a',
    },
    'nix-flake-c' : {
      'subproj' : 'libflake-c',
      'archive' : 'libnixflakec.a',
    },
    'nix-main-c' : {
      'subproj' : 'libmain-c',
      'archive' : 'libnixmainc.a',
    },
  }

  foreach name, info : c_api_libs
    dep = dependency(name)
    if dep.type_name() == 'internal'
      # A sane limitation.
      if get_option('default_library') not in [ 'static', 'both' ]
        error(
          'plugin-c-api=true requires building with -Ddefault_library=static or -Ddefault_library=both',
        )
      endif
      # Fish out the static library for link_whole.
      lib = subproject(info['subproj']).get_variable('this_library')
      if get_option('default_library') == 'both'
        lib = lib.get_static_lib()
      endif
      link_whole += lib
    else
      # For pkg-config dependency (in componentised builds), just link manually.
      libfile = info['archive']
      if host_machine.system() == 'darwin'
        link_args += [
          '-Wl,-force_load,' + dep.get_variable('libdir') / libfile,
        ]
      elif not host_machine.system().startswith('windows')
        link_args += [ '-Wl,--whole-archive' ]
        link_args += [ dep.get_variable('libdir') / libfile ]
        link_args += [ '-Wl,--no-whole-archive' ]
      endif
    endif
  endforeach
endif

mimalloc = dependency('mimalloc', required : get_option('mimalloc'))
if mimalloc.found()
  deps_private += mimalloc
endif

subdir('nix-meson-build-support/export-all-symbols')
subdir('nix-meson-build-support/windows-version')

configdata = configuration_data()

# The CLI has a more detailed version string than the libraries; see `nixVersion`
configdata.set_quoted('NIX_CLI_VERSION', meson.project_version())

fs = import('fs')
prefix = get_option('prefix')

bindir = get_option('bindir')
bindir = fs.is_absolute(bindir) ? bindir : prefix / bindir
configdata.set_quoted('NIX_BIN_DIR', bindir)

mandir = get_option('mandir')
mandir = fs.is_absolute(mandir) ? mandir : prefix / mandir
configdata.set_quoted('NIX_MAN_DIR', mandir)

configdata.set_quoted('FALLBACK_BASH', get_option('bash-program'))

config_priv_h = configure_file(
  configuration : configdata,
  output : 'cli-config-private.hh',
)

subdir('nix-meson-build-support/common')
subdir('nix-meson-build-support/generate-header')

nix_sources = [ config_priv_h ] + files(
  'add-to-store.cc',
  'app.cc',
  'build-trace.cc',
  'build.cc',
  'bundle.cc',
  'cat.cc',
  'config-check.cc',
  'config.cc',
  'copy.cc',
  'crash-handler.cc',
  'derivation-add.cc',
  'derivation-show.cc',
  'derivation.cc',
  'develop.cc',
  'diff-closures.cc',
  'dump-path.cc',
  'edit.cc',
  'env.cc',
  'eval.cc',
  'flake-prefetch-inputs.cc',
  'flake.cc',
  'formatter.cc',
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
  'registry.cc',
  'repl.cc',
  'run.cc',
  'search.cc',
  'self-exe.cc',
  'sigs.cc',
  'store-copy-log.cc',
  'store-delete.cc',
  'store-gc.cc',
  'store-info.cc',
  'store-repair.cc',
  'store-submit-output.cc',
  'store.cc',
  'upgrade-nix.cc',
  'verify.cc',
  'version.cc',
  'why-depends.cc',
)

if host_machine.system() != 'windows'
  nix_sources += files(
    'unix/daemon.cc',
    'unix/store-roots-daemon.cc',
  )
endif

nix_sources += [
  # TODO: Get rid of this for good. It's used in profile.md with
  # string literal concatenation.
  gen_header.process('profiles.md'),
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

include_dirs = [ include_directories('.') ]

link_args += linker_export_flags
if host_machine.system() == 'darwin'
  # Bump the executable's stack size to facilitate deep recursion in the evaluator.
  # This is done statically so that we can use posix_spawn on macOS.
  link_args += '-Wl,-stack_size,0x3C00000'
endif

this_exe = executable(
  meson.project_name(),
  sources,
  dependencies : deps_private_subproject + deps_private + deps_private_no_pkgconfig,
  include_directories : include_dirs,
  link_args : link_args,
  link_whole : link_whole,
  # Export symbols to have C++ plugins working, even when internal libraries
  # are linked statically into the executable.
  export_dynamic : true,
  install : true,
  cpp_pch : do_pch ? [ 'pch/precompiled-headers.hh' ] : [],
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

nix_symlinks_targets = []
foreach linkname : nix_symlinks
  install_symlink(
    linkname + executable_suffix,
    # TODO(Qyriad): should these continue to be relative symlinks?
    pointing_to : fs.name(this_exe),
    install_dir : get_option('bindir'),
    # The 'runtime' tag is what executables default to, which we want to emulate here.
    install_tag : 'runtime',
  )
  symlink_target = custom_target(
    command : [ 'ln', '-sf', fs.name(this_exe), '@OUTPUT@' ],
    output : linkname + executable_suffix,
    # native doesn't allow dangling symlinks, but the target executable often doesn't exist at this time
    env : {'MSYS' : 'winsymlinks:lnk'},
    # TODO(Ericson2314): Don't do this once we have the `meson.override_find_program` working)
    build_by_default : true,
    depends : this_exe,
  )
  # TODO(Ericson3214): Doesn't yet work
  #meson.override_find_program(linkname, t)
  nix_symlinks_targets += symlink_target
endforeach

install_symlink(
  'build-remote',
  pointing_to : '..' / '..' / get_option('bindir') / fs.name(this_exe),
  install_dir : get_option('libexecdir') / fs.name(this_exe),
  # The 'runtime' tag is what executables default to, which we want to emulate here.
  install_tag : 'runtime',
)

custom_target(
  command : [ 'ln', '-sf', fs.name(this_exe), '@OUTPUT@' ],
  output : 'build-remote' + executable_suffix,
  # native doesn't allow dangling symlinks, but the target executable often doesn't exist at this time
  env : {'MSYS' : 'winsymlinks:lnk'},
  # TODO(Ericson2314): Don't do this once we have the `meson.override_find_program` working)
  build_by_default : true,
  depends : this_exe,
)
# TODO(Ericson3214): Doesn't yet work
#meson.override_find_program(linkname, t)

localstatedir = nix_store.get_variable('localstatedir')
store_dir = nix_store.get_variable('storedir')
subdir('scripts')
subdir('misc')
