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
  'lexer-helpers.cc',
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
    # Unity builds mess up the recursive header dependency between flex and bison generated code.
    'unity=off',
  ],
)

