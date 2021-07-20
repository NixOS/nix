# Nix lib expr build file
#============================================================================

libexpr_dir = meson.current_source_dir()
libexpr_inc = proj_inc


# dependencies
#============================================================================

libexpr_dep_list = [
    boost_dep,
    libdl_dep,
    libsodium_dep,
    gc_dep,

    libutil_dep,
    libstore_dep,
    libmain_dep,
    libfetchers_dep]


# src files
#============================================================================

libexpr_src = files(
    join_paths(libexpr_dir, 'attr-path.cc'),
    join_paths(libexpr_dir, 'attr-set.cc'),
    join_paths(libexpr_dir, 'common-eval-args.cc'),
    join_paths(libexpr_dir, 'eval-cache.cc'),
    join_paths(libexpr_dir, 'eval.cc'),
    join_paths(libexpr_dir, 'function-trace.cc'),
    join_paths(libexpr_dir, 'get-drvs.cc'),
    join_paths(libexpr_dir, 'json-to-value.cc'),
    join_paths(libexpr_dir, 'nixexpr.cc'),
    join_paths(libexpr_dir, 'primops.cc'),
    join_paths(libexpr_dir, 'value-to-json.cc'),
    join_paths(libexpr_dir, 'value-to-xml.cc'))


libexpr_headers = files(
    join_paths(libexpr_dir, 'attr-path.hh'),
    join_paths(libexpr_dir, 'attr-set.hh'),
    join_paths(libexpr_dir, 'common-eval-args.hh'),
    join_paths(libexpr_dir, 'eval-cache.hh'),
    join_paths(libexpr_dir, 'eval.hh'),
    join_paths(libexpr_dir, 'eval-inline.hh'),
    join_paths(libexpr_dir, 'function-trace.hh'),
    join_paths(libexpr_dir, 'get-drvs.hh'),
    join_paths(libexpr_dir, 'json-to-value.hh'),
    join_paths(libexpr_dir, 'nixexpr.hh'),
    join_paths(libexpr_dir, 'primops.hh'),
    join_paths(libexpr_dir, 'symbol-table.hh'),
    join_paths(libexpr_dir, 'value.hh'),
    join_paths(libexpr_dir, 'value-to-json.hh'),
    join_paths(libexpr_dir, 'value-to-xml.hh'))


# include directories
#========================================================================

# include current dir
#---------------------------------------------------
libexpr_inc += include_directories('.')


# add subdirectories
#---------------------------------------------------
libexpr_dirs = [
  'flake',
  'primops']

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
        proj_inc],
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
# libexpr_config = pkg.generate(
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
