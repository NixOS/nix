project('nix-expr-c', 'cpp',
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
deps_private_subproject = [ ]

# See note in ../nix-util/meson.build
deps_public = [ ]

# See note in ../nix-util/meson.build
deps_public_subproject = [ ]

# See note in ../nix-util/meson.build
deps_other = [ ]

configdata = configuration_data()

foreach nix_dep : [
  dependency('nix-util'),
  dependency('nix-store'),
  dependency('nix-expr'),
]
  if nix_dep.type_name() == 'internal'
    deps_private_subproject += nix_dep
    # subproject sadly no good for pkg-config module
    deps_other += nix_dep
  else
    deps_private += nix_dep
  endif
endforeach

foreach nix_dep : [
  dependency('nix-util-c'),
  dependency('nix-store-c'),
]
  if nix_dep.type_name() == 'internal'
    deps_public_subproject += nix_dep
    # subproject sadly no good for pkg-config module
    deps_other += nix_dep
  else
    deps_public += nix_dep
  endif
endforeach

config_h = configure_file(
  configuration : configdata,
  output : 'config-expr.h',
)

add_project_arguments(
  # TODO(Qyriad): Yes this is how the autoconf+Make system did it.
  # It would be nice for our headers to be idempotent instead.

  # From C++ libraries, only for internals
  '-include', 'config-util.hh',
  '-include', 'config-store.hh',
  '-include', 'config-expr.hh',

  # From C libraries, for our public, installed headers too
  '-include', 'config-util.h',
  '-include', 'config-store.h',
  '-include', 'config-expr.h',
  '-Wno-deprecated-declarations',
  '-Wimplicit-fallthrough',
  '-Werror=switch',
  '-Werror=switch-enum',
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
  'nix_api_expr.cc',
  'nix_api_external.cc',
  'nix_api_value.cc',
)

include_dirs = [include_directories('.')]

headers = [config_h] + files(
  'nix_api_expr.h',
  'nix_api_external.h',
  'nix_api_value.h',
)

if host_machine.system() == 'cygwin' or host_machine.system() == 'windows'
  # Windows DLLs are stricter ab_subprojectout symbol visibility than Unix shared
  # objects --- see https://gcc.gnu.org/wiki/Visibility for details.
  # This is a temporary sledgehammer to export everything like on Unix,
  # and not detail with this yet.
  #
  # TODO do not do this, and instead do fine-grained export annotations.
  linker_export_flags = ['-Wl,--export-all-symbols']
else
  linker_export_flags = []
endif

this_library = library(
  'nixexprc',
  sources,
  dependencies : deps_public + deps_private + deps_other,
  include_directories : include_dirs,
  link_args: linker_export_flags,
  install : true,
)

install_headers(headers, subdir : 'nix', preserve_path : true)

requires_private = []
foreach dep : deps_private_subproject
  requires_private  += dep.name()
endforeach
requires_private += deps_private

requires_public  = []
foreach dep : deps_public_subproject
  requires_public  += dep.name()
endforeach
requires_public += deps_public

import('pkgconfig').generate(
  this_library,
  filebase : meson.project_name(),
  name : 'Nix',
  description : 'Nix Package Manager',
  subdirs : ['nix'],
  extra_cflags : ['-std=c++2a'],
  requires : requires_public,
  requires_private : requires_private,
)

meson.override_dependency(meson.project_name(), declare_dependency(
  include_directories : include_dirs,
  link_with : this_library,
  compile_args : ['-std=c++2a'],
  dependencies : [],
))
