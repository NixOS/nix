project('nix-expr', 'cpp',
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

configdata = configuration_data()

foreach nix_dep : [
  dependency('nix-util'),
  dependency('nix-store'),
  dependency('nix-fetchers'),
]
  if nix_dep.type_name() == 'internal'
    deps_public_subproject += nix_dep
    # subproject sadly no good for pkg-config module
    deps_other += nix_dep
  else
    deps_public += nix_dep
  endif
endforeach

# This is only conditional to work around
# https://github.com/mesonbuild/meson/issues/13293. It should be
# unconditional.
if not (host_machine.system() == 'windows' and cxx.get_id() == 'gcc')
  deps_private += dependency('threads')
endif

boost = dependency(
  'boost',
  modules : ['container', 'context'],
)
# boost is a public dependency, but not a pkg-config dependency unfortunately, so we
# put in `deps_other`.
deps_other += boost

nlohmann_json = dependency('nlohmann_json', version : '>= 3.9')
deps_public += nlohmann_json

bdw_gc = dependency('bdw-gc', required : get_option('gc'))
if bdw_gc.found()
  deps_public += bdw_gc
  foreach funcspec : [
    'pthread_attr_get_np',
    'pthread_getattr_np',
  ]
    define_name = 'HAVE_' + funcspec.underscorify().to_upper()
    define_value = cxx.has_function(funcspec).to_int()
    configdata.set(define_name, define_value)
  endforeach
  configdata.set('GC_THREADS', 1)
endif
configdata.set('HAVE_BOEHMGC', bdw_gc.found().to_int())

config_h = configure_file(
  configuration : configdata,
  output : 'config-expr.hh',
)

add_project_arguments(
  # TODO(Qyriad): Yes this is how the autoconf+Make system did it.
  # It would be nice for our headers to be idempotent instead.
  '-include', 'config-util.hh',
  '-include', 'config-store.hh',
  # '-include', 'config-fetchers.h',
  '-include', 'config-expr.hh',
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

parser_tab = custom_target(
  input : 'parser.y',
  output : [
    'parser-tab.cc',
    'parser-tab.hh',
  ],
  command : [
    'bison',
    '-v',
    '-o',
    '@OUTPUT0@',
    '@INPUT@',
    '-d',
  ],
  # NOTE(Qyriad): Meson doesn't support installing only part of a custom target, so we add
  # an install script below which removes parser-tab.cc.
  install : true,
  install_dir : get_option('includedir') / 'nix',
)

lexer_tab = custom_target(
  input : [
    'lexer.l',
    parser_tab,
  ],
  output : [
    'lexer-tab.cc',
    'lexer-tab.hh',
  ],
  command : [
    'flex',
    '--outfile',
    '@OUTPUT0@',
    '--header-file=' + '@OUTPUT1@',
    '@INPUT0@',
  ],
  # NOTE(Qyriad): Meson doesn't support installing only part of a custom target, so we add
  # an install script below which removes lexer-tab.cc.
  install : true,
  install_dir : get_option('includedir') / 'nix',
)

generated_headers = []
foreach header : [
  'imported-drv-to-derivation.nix',
  'fetchurl.nix',
  'flake/call-flake.nix',
  'primops/derivation.nix',
]
  generated_headers += custom_target(
    command : [ 'bash', '-c', '{ echo \'R"__NIX_STR(\' && cat @INPUT@ && echo \')__NIX_STR"\'; } > "$1"', '_ignored_argv0', '@OUTPUT@' ],
    input : header,
    output : '@PLAINNAME@.gen.hh',
  )
endforeach


sources = files(
  'attr-path.cc',
  'attr-set.cc',
  'eval-cache.cc',
  'eval-error.cc',
  'eval-gc.cc',
  'eval-settings.cc',
  'eval.cc',
  'function-trace.cc',
  'get-drvs.cc',
  'json-to-value.cc',
  'nixexpr.cc',
  'paths.cc',
  'primops.cc',
  'primops/context.cc',
  'primops/fetchClosure.cc',
  'primops/fetchMercurial.cc',
  'primops/fetchTree.cc',
  'primops/fromTOML.cc',
  'print-ambiguous.cc',
  'print.cc',
  'search-path.cc',
  'value-to-json.cc',
  'value-to-xml.cc',
  'value/context.cc',
)

headers = [config_h] + files(
  'attr-path.hh',
  'attr-set.hh',
  'eval-cache.hh',
  'eval-error.hh',
  'eval-gc.hh',
  'eval-inline.hh',
  'eval-settings.hh',
  'eval.hh',
  'function-trace.hh',
  'gc-small-vector.hh',
  'get-drvs.hh',
  'json-to-value.hh',
  'nixexpr.hh',
  'parser-state.hh',
  'pos-idx.hh',
  'pos-table.hh',
  'primops.hh',
  'print-ambiguous.hh',
  'print-options.hh',
  'print.hh',
  'repl-exit-status.hh',
  'search-path.hh',
  'symbol-table.hh',
  'value-to-json.hh',
  'value-to-xml.hh',
  'value.hh',
  'value/context.hh',
)

this_library = library(
  'nixexpr',
  sources,
  parser_tab,
  lexer_tab,
  generated_headers,
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
  libraries_private : ['-lboost_container', '-lboost_context'],
)

meson.override_dependency(meson.project_name(), declare_dependency(
  include_directories : include_directories('.'),
  link_with : this_library,
  compile_args : ['-std=c++2a'],
  dependencies : deps_public_subproject + deps_public,
))
