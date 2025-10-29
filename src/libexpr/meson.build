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
  'value.cc',
  'value/context.cc',
)

subdir('include/nix/expr')

subdir('primops')

subdir('nix-meson-build-support/export-all-symbols')
subdir('nix-meson-build-support/windows-version')

# Turns out that Bison/Flex are particularly sensitive to compilers
# failing to inline functions. For that reason we crank up the inlining
# threshold manually for optimized builds. Yes, this can be considered 'ricing'
# the compiler, but it does pay off.
#
# NOTE: missed inlining can be spotted (for Clang) using -Rpass-missed=inline
# and -fdump-ipa-inline-missed (for GCC).
parser_library_cpp_args = []

if not get_option('debug')
  if cxx.get_id() == 'clang'
    # The default as of LLVM 21 is 225:
    # llc --help-hidden | grep inline-threshold
    parser_library_cpp_args += [
      '-mllvm',
      '-inline-threshold=5000',
    ]
  elif cxx.get_id() == 'gcc'
    parser_library_cpp_args += [
      '--param=max-inline-insns-single=1000',
      '--param=max-inline-insns-auto=1000',
      '--param=inline-unit-growth=400',
    ]
  endif
endif

# Working around https://github.com/mesonbuild/meson/issues/1367.
parser_library = static_library(
  'nixexpr-parser',
  parser_tab,
  lexer_tab,
  cpp_args : parser_library_cpp_args,
  dependencies : deps_public + deps_private + deps_other,
  include_directories : include_dirs,
  # 1. Stdlib and regular assertions regress parser performance significantly, so build without
  # them for this one library when building in a release configuration.
  # 2. Disable LTO for GCC because then inlining flags won't apply, since LTO in GCC is done
  # by plonking down GIMPLE in the archive.
  override_options : [
    'b_ndebug=@0@'.format(not get_option('debug')),
    'b_lto=@0@'.format(get_option('b_lto') and cxx.get_id() != 'gcc'),
  ],
)

this_library = library(
  'nixexpr',
  sources,
  config_priv_h,
  parser_tab[1],
  lexer_tab[1],
  generated_headers,
  soversion : nix_soversion,
  dependencies : deps_public + deps_private + deps_other,
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
