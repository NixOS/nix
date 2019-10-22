# Nix lib expr build file
#============================================================================




# src files
#============================================================================

src_inc += include_directories('.', 'primops')

libexpr_src = files(
    join_paths(meson.source_root(), 'src/libexpr/primops/context.cc'),
    join_paths(meson.source_root(), 'src/libexpr/primops/fetchGit.cc'),
    join_paths(meson.source_root(), 'src/libexpr/primops/fetchMercurial.cc'),
    join_paths(meson.source_root(), 'src/libexpr/primops/fromTOML.cc'),

    join_paths(meson.source_root(), 'src/libexpr/attr-path.cc'),
    join_paths(meson.source_root(), 'src/libexpr/attr-set.cc'),
    join_paths(meson.source_root(), 'src/libexpr/common-eval-args.cc'),
    join_paths(meson.source_root(), 'src/libexpr/eval.cc'),
    join_paths(meson.source_root(), 'src/libexpr/get-drvs.cc'),
    join_paths(meson.source_root(), 'src/libexpr/json-to-value.cc'),
    join_paths(meson.source_root(), 'src/libexpr/names.cc'),
    join_paths(meson.source_root(), 'src/libexpr/nixexpr.cc'),
    join_paths(meson.source_root(), 'src/libexpr/primops.cc'),
    join_paths(meson.source_root(), 'src/libexpr/value-to-json.cc'),
    join_paths(meson.source_root(), 'src/libexpr/value-to-xml.cc'))

libexpr_headers = files(
    join_paths(meson.source_root(), 'src/libexpr/attr-path.hh'),
    join_paths(meson.source_root(), 'src/libexpr/attr-set.hh'),
    join_paths(meson.source_root(), 'src/libexpr/common-eval-args.hh'),
    join_paths(meson.source_root(), 'src/libexpr/eval.hh'),
    join_paths(meson.source_root(), 'src/libexpr/eval-inline.hh'),
    join_paths(meson.source_root(), 'src/libexpr/function-trace.hh'),
    join_paths(meson.source_root(), 'src/libexpr/get-drvs.hh'),
    join_paths(meson.source_root(), 'src/libexpr/json-to-value.hh'),
    join_paths(meson.source_root(), 'src/libexpr/names.hh'),
    join_paths(meson.source_root(), 'src/libexpr/nixexpr.hh'),
    join_paths(meson.source_root(), 'src/libexpr/primops.hh'),
    join_paths(meson.source_root(), 'src/libexpr/symbol-table.hh'),
    join_paths(meson.source_root(), 'src/libexpr/value.hh'),
    join_paths(meson.source_root(), 'src/libexpr/value-to-json.hh'),
    join_paths(meson.source_root(), 'src/libexpr/value-to-xml.hh'))




# dependancies
#============================================================================

libexpr_dep_list = [
    libdl_dep,
    libsodium_dep]

if sys_name == 'freebsd'
    libexpr_dep_list += libdl_dep
endif




# Link args
#============================================================================

libexpr_link_list = [
    libutil_lib,
    libstore_lib,
    libmain_lib]

libexpr_link_args = []




# compiler args
#============================================================================

libexpr_cxx_args = []




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
        '@INPUT@',
        '-d'])

libexpr_src += custom_target(
    'lexer_tab.[cchh]',
    output : ['lexer-tab.cc', 'lexer-tab.hh'],
    input : 'lexer.l',
    command : [
        flex,
        '--outfile=@OUTPUT0@',
        '--header-file=@OUTPUT1@',
        '@INPUT@'])




# build
#============================================================================

libexpr_lib = library(
    'nixexpr',
    install : true,
    install_mode : 'rwxr-xr-x',
    install_dir : libdir,
    include_directories : src_inc,
    link_with : libexpr_link_list,
    sources : libexpr_src,
    link_args : libexpr_link_args,
    dependencies : libexpr_dep_list)

install_headers(
    libexpr_headers,
    install_dir : join_paths(includedir, 'nix'))
