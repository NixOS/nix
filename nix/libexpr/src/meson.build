# Nix lib expr build file
#============================================================================


# dependencies
#============================================================================

libexpr_dep_list = [
    boost_dep,
    libdl_dep,
    libsodium_dep,
    gc_dep,
    toml11_dep,
    
    libutil_dep,
    libstore_dep,
    libmain_dep,
    libfetchers_dep]


# src files
#============================================================================

libexpr_src = files(
    'attr-path.cc',
    'attr-set.cc',
    'eval-cache.cc',
    'eval.cc',
    'eval-settings.cc',
    'function-trace.cc',
    'get-drvs.cc',
    'json-to-value.cc',
    'nixexpr.cc',
    'paths.cc',
    'primops.cc',
    'print.cc',
    'search-path.cc',
    'value-to-json.cc',
    'value-to-xml.cc')

libexpr_headers = files(
    'attr-path.hh',
    'attr-set.hh',
    'eval-cache.hh',
    'eval.hh',
    'eval-inline.hh',
    'eval-settings.hh',
    'function-trace.hh',
    'get-drvs.hh',
    'json-to-value.hh',
    'nixexpr.hh',
    'primops.hh',
    'print.hh',
    'search-path.hh',
    'symbol-table.hh',
    'value.hh',
    'value-to-json.hh',
    'value-to-xml.hh')


# include directories
#========================================================================

# include current dir
#---------------------------------------------------
libexpr_inc = [include_directories('.')]

# add subdirectories
#---------------------------------------------------
libexpr_dirs = [
  'flake',
  'primops',
  'value']

foreach dir : libexpr_dirs
    subdir(dir)
endforeach


# targets
#============================================================================

libexpr_src += custom_target(
    'parser_tab.[cchh]',
    output : [
        'parser-tab.cc',
        'parser-tab.hh'],
    input : 'parser.y',
    command : [
        bison,
        '-v',
        '--output=@OUTPUT0@',
        '--defines=@OUTPUT1@',
        '@INPUT@'])

libexpr_src += custom_target(
    'lexer_tab.[cchh]',
    output : ['lexer-tab.cc', 'lexer-tab.hh'],
    input : 'lexer.l',
    command : [
        flex,
        '--outfile=@OUTPUT0@',
        '--header-file=@OUTPUT1@',
        '@INPUT@'])

libexpr_src += custom_target(
  'imported-drv-to-derivation.nix.gen.hh',
  output : 'imported-drv-to-derivation.nix.gen.hh',
  input : 'imported-drv-to-derivation.nix',
  command : [bash, '-c', gen_rheader, 'sh', '@OUTPUT@'])


libexpr_src += custom_target(
  'fetchurl.nix.gen.hh',
  output : 'fetchurl.nix.gen.hh',
  input : 'fetchurl.nix',
  command : [bash, '-c', gen_rheader, 'sh', '@OUTPUT@'])


# build
#============================================================================

# set build args
#---------------------------------------------------
libexpr_cxx_args = []

libexpr_link_args = []


# build library
#---------------------------------------------------
libexpr_lib = library(
    'nixexpr',
    sources : libexpr_src,
    install : true,
    install_mode : 'rwxr-xr-x',
    install_dir : libdir,
    include_directories : [
        libexpr_inc,
        proj_inc,
    ],
    cpp_args : libexpr_cxx_args,
    link_args : libexpr_link_args,
    dependencies : libexpr_dep_list)

# install headers
#---------------------------------------------------
install_headers(
    libexpr_headers,
    install_dir : join_paths(includedir, 'nix'))


# generate pkg-config
#---------------------------------------------------
# libexpr_config = pkgconfig.generate(
#     libexpr_lib,
#     libraries : [
#         libstore_lib],
#     version : meson.project_version(),
#     name : 'Nix',
#     subdirs : ['nix/'],
#     filebase : 'nix-expr',
#     extra_cflags : '-std=c++17',
#     description : 'Nix Package Manager.')


# declare dependency
#---------------------------------------------------
libexpr_dep = declare_dependency(
    link_with : libexpr_lib,
    include_directories : include_directories('.'))
