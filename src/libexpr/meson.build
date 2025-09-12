project(
  'nix-expr',
  'cpp',
  version : files('.version'),
  default_options : [
    'cpp_std=c++23',
    # TODO(Qyriad): increase the warning level
    'warning_level=1',
    'errorlogs=true', # Please print logs for tests that fail
  ],
  meson_version : '>= 1.1',
  license : 'LGPL-2.1-or-later',
)

cxx = meson.get_compiler('cpp')

subdir('nix-meson-build-support/deps-lists')

configdata_pub = configuration_data()
configdata_priv = configuration_data()

deps_private_maybe_subproject = []
deps_public_maybe_subproject = [
  dependency('nix-util'),
  dependency('nix-store'),
  dependency('nix-fetchers'),
]
subdir('nix-meson-build-support/subprojects')
subdir('nix-meson-build-support/big-objs')

# Check for each of these functions, and create a define like `#define HAVE_LCHOWN 1`.
check_funcs = [
  'sysconf',
]
foreach funcspec : check_funcs
  define_name = 'HAVE_' + funcspec.underscorify().to_upper()
  define_value = cxx.has_function(funcspec).to_int()
  configdata_priv.set(define_name, define_value)
endforeach

boost = dependency(
  'boost',
  modules : [
    'container',
    'context',
  ],
  include_type : 'system',
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
    configdata_priv.set(define_name, define_value)
  endforeach
endif
# Used in public header. Affects ABI!
configdata_pub.set('NIX_USE_BOEHMGC', bdw_gc.found().to_int())

toml11 = dependency(
  'toml11',
  version : '>=3.7.0',
  method : 'cmake',
  include_type : 'system',
)

configdata_priv.set(
  'HAVE_TOML11_4',
  toml11.version().version_compare('>= 4.0.0').to_int(),
)

deps_other += toml11

config_priv_h = configure_file(
  configuration : configdata_priv,
  output : 'expr-config-private.hh',
)

subdir('nix-meson-build-support/common')

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
    '-Cf', # Use full scanner tables
    '--outfile', '@OUTPUT0@',
    '--header-file=' + '@OUTPUT1@',
    '@INPUT0@',
  ],
  # NOTE(Qyriad): Meson doesn't support installing only part of a custom target, so we add
  # an install script below which removes lexer-tab.cc.
  install : true,
  install_dir : get_option('includedir') / 'nix',
)

subdir('nix-meson-build-support/generate-header')

generated_headers = []
foreach header : [
  'imported-drv-to-derivation.nix',
  'fetchurl.nix',
]
  generated_headers += gen_header.process(header)
endforeach

sources = files(
  'attr-path.cc',
  'attr-set.cc',
  'eval-cache.cc',
  'eval-error.cc',
  'eval-gc.cc',
  'eval-profiler-settings.cc',
  'eval-profiler.cc',
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

subdir('include/nix/expr')

subdir('primops')

subdir('nix-meson-build-support/export-all-symbols')
subdir('nix-meson-build-support/windows-version')

this_library = library(
  'nixexpr',
  sources,
  config_priv_h,
  parser_tab,
  lexer_tab,
  generated_headers,
  dependencies : deps_public + deps_private + deps_other,
  include_directories : include_dirs,
  link_args : linker_export_flags,
  prelink : true, # For C++ static initializers
  install : true,
  cpp_pch : do_pch ? [ 'pch/precompiled-headers.hh' ] : [],
)

install_headers(headers, subdir : 'nix/expr', preserve_path : true)

libraries_private = []

subdir('nix-meson-build-support/export')
