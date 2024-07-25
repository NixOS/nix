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

subdir('build-utils-meson/deps-lists')

configdata = configuration_data()

deps_private_maybe_subproject = [
]
deps_public_maybe_subproject = [
  dependency('nix-util'),
  dependency('nix-store'),
  dependency('nix-fetchers'),
]
subdir('build-utils-meson/subprojects')

subdir('build-utils-meson/threads')

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

toml11 = dependency('toml11', version : '>=3.7.0', method : 'cmake')
deps_other += toml11

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
  language : 'cpp',
)

subdir('build-utils-meson/diagnostics')

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

subdir('build-utils-meson/generate-header')

generated_headers = []
foreach header : [
  'imported-drv-to-derivation.nix',
  'fetchurl.nix',
  'call-flake.nix',
]
  generated_headers += gen_header.process(header)
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
  'lexer-helpers.cc',
  'nixexpr.cc',
  'paths.cc',
  'primops.cc',
  'print-ambiguous.cc',
  'print.cc',
  'search-path.cc',
  'value-to-json.cc',
  'value-to-xml.cc',
  'value/context.cc',
)

include_dirs = [include_directories('.')]

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
  # internal: 'lexer-helpers.hh',
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

subdir('primops')

this_library = library(
  'nixexpr',
  sources,
  parser_tab,
  lexer_tab,
  generated_headers,
  dependencies : deps_public + deps_private + deps_other,
  prelink : true, # For C++ static initializers
  install : true,
)

install_headers(headers, subdir : 'nix', preserve_path : true)

libraries_private = []

subdir('build-utils-meson/export')
