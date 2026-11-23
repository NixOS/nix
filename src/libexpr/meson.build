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
  meson_version : '>= 1.8',
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

# Check for each of these functions, and create a define like `#define HAVE_SYSCONF 1`.
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
# boost is a public dependency, but not a pkg-config dependency unfortunately.
deps_public_no_pkgconfig += boost

nlohmann_json = dependency('nlohmann_json', version : '>= 3.9')
deps_public += nlohmann_json

# eval-cache uses `INSERT ... RETURNING` which requires >= 3.35
sqlite = dependency('sqlite3', 'sqlite', version : '>= 3.35')
deps_private += sqlite

bdw_gc_required = get_option('gc').disable_if(
  'address' in get_option('b_sanitize'),
  error_message : 'Building with Boehm GC and ASAN is not supported',
)

bdw_gc = dependency('bdw-gc', required : bdw_gc_required)
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
  if host_machine.system() == 'cygwin'
    # undefined reference to `__wrap__Znwm'
    configdata_pub.set('GC_NO_INLINE_STD_NEW', 1)
  endif
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

deps_private_no_pkgconfig += toml11

cpuid = dependency('libcpuid', 'cpuid', required : false)
configdata_priv.set('HAVE_LIBCPUID', cpuid.found().to_int())
deps_private += cpuid

config_priv_h = configure_file(
  configuration : configdata_priv,
  output : 'expr-config-private.hh',
)

subdir('nix-meson-build-support/common')

sources = files(
  'attr-path.cc',
  'attr-set.cc',
  'diagnose.cc',
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
  'nixexpr.cc',
  'paths.cc',
  'primops.cc',
  'print-ambiguous.cc',
  'print.cc',
  'register-library-versions.cc',
  'search-path.cc',
  'value-to-json.cc',
  'value-to-xml.cc',
  'value.cc',
  'value/context.cc',
)

subdir('include/nix/expr')

subdir('nix-meson-build-support/export-all-symbols')
subdir('nix-meson-build-support/windows-version')

subdir('primops')
subdir('parser')

this_library = library(
  'nixexpr',
  sources,
  config_priv_h,
  soversion : nix_soversion,
  dependencies : deps_public + deps_private + deps_public_no_pkgconfig + deps_private_no_pkgconfig,
  include_directories : include_dirs,
  link_args : linker_export_flags,
  link_whole : [ parser_library ],
  prelink : true, # For C++ static initializers
  install : true,
  cpp_pch : do_pch ? [ 'pch/precompiled-headers.hh' ] : [],
)

install_headers(headers, subdir : 'nix/expr', preserve_path : true)

libraries_private = []

subdir('nix-meson-build-support/export')
