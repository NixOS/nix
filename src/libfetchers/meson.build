project('nix-fetchers', 'cpp',
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

# See note in ../nix-util/meson.build
deps_private = [ ]

# See note in ../nix-util/meson.build
deps_public = [ ]

# See note in ../nix-util/meson.build
deps_public_subproject = [ ]

# See note in ../nix-util/meson.build
deps_other = [ ]

foreach nix_dep : [
  dependency('nix-util'),
  dependency('nix-store'),
]
  if nix_dep.type_name() == 'internal'
    deps_public_subproject += nix_dep
    # subproject sadly no good for pkg-config module
    deps_other += nix_dep
  else
    deps_public += nix_dep
  endif
endforeach

nlohmann_json = dependency('nlohmann_json', version : '>= 3.9')
deps_public += nlohmann_json

libgit2 = dependency('libgit2')
deps_public += libgit2

add_project_arguments(
  # TODO(Qyriad): Yes this is how the autoconf+Make system did it.
  # It would be nice for our headers to be idempotent instead.
  '-include', 'config-util.hh',
  '-include', 'config-store.hh',
  # '-include', 'config-fetchers.h',
  '-Wno-deprecated-declarations',
  '-Wimplicit-fallthrough',
  '-Werror=switch',
  '-Werror=switch-enum',
  '-Werror=unused-result',
  '-Wdeprecated-copy',
  '-Wignored-qualifiers',
  # Enable assertions in libstdc++ by default. Harmless on libc++. Benchmarked
  # at ~1% overhead in `nix search`.
  #
  # FIXME: remove when we get meson 1.4.0 which will default this to on for us:
  # https://mesonbuild.com/Release-notes-for-1-4-0.html#ndebug-setting-now-controls-c-stdlib-assertions
  '-D_GLIBCXX_ASSERTIONS=1',
  language : 'cpp',
)

sources = files(
  'attrs.cc',
  'cache.cc',
  'fetch-settings.cc',
  'fetch-to-store.cc',
  'fetchers.cc',
  'filtering-source-accessor.cc',
  'git.cc',
  'git-utils.cc',
  'github.cc',
  'indirect.cc',
  'mercurial.cc',
  'mounted-source-accessor.cc',
  'path.cc',
  'store-path-accessor.cc',
  'registry.cc',
  'tarball.cc',
)

headers = files(
  'attrs.hh',
  'cache.hh',
  'fetch-settings.hh',
  'fetch-to-store.hh',
  'filtering-source-accessor.hh',
  'git-utils.hh',
  'mounted-source-accessor.hh',
  'fetchers.hh',
  'registry.hh',
  'store-path-accessor.hh',
  'tarball.hh',
)

this_library = library(
  'nixfetchers',
  sources,
  dependencies : deps_public + deps_private + deps_other,
  install : true,
)

install_headers(headers, subdir : 'nix', preserve_path : true)

requires = []
foreach dep : deps_public_subproject
  requires += dep.name()
endforeach
requires += deps_public

import('pkgconfig').generate(
  this_library,
  filebase : meson.project_name(),
  name : 'Nix',
  description : 'Nix Package Manager',
  subdirs : ['nix'],
  extra_cflags : ['-std=c++2a'],
  requires : requires,
  requires_private : deps_private,
)

meson.override_dependency(meson.project_name(), declare_dependency(
  include_directories : include_directories('.'),
  link_with : this_library,
  compile_args : ['-std=c++2a'],
  dependencies : deps_public_subproject + deps_public,
))
