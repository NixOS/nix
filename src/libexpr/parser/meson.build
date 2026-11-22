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
  install : false,
)

lexer_tab = custom_target(
  input : [
    'lexer.cc',
    parser_tab,
  ],
  output : [
    'lexer-tab.cc',
  ],
  command : [
    're2c',
    '--conditions',
    '--output', '@OUTPUT0@',
    '@INPUT0@',
  ],
  install : false,
)

# Turns out that Bison is particularly sensitive to compilers
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
  'parser.cc',
  cpp_args : parser_library_cpp_args,
  dependencies : deps_public + deps_private + deps_public_no_pkgconfig + deps_private_no_pkgconfig,
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

