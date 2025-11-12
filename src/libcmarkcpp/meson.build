project(
  'cmark-cpp',
  'cpp',
  version : '0.1',
  default_options : [
    'cpp_std=c++20',
    'warning_level=1',
  ],
  meson_version : '>= 1.1',
  license : 'ISC',
)

cxx = meson.get_compiler('cpp')

# CMark dependency
cmark = dependency('libcmark', required : true)

sources = files(
  'cmark-terminal.cc',
)

headers = files(
  'include/cmark/cmark-cpp.hh',
  'include/cmark/cmark-terminal.hh',
)

include_dirs = include_directories('include')

libcmarkcpp = library(
  'cmarkcpp',
  sources,
  dependencies : cmark,
  include_directories : include_dirs,
  install : true,
  version : meson.project_version(),
)

install_headers(headers, subdir : 'cmark')

# Make this available as a dependency for meson projects
meson.override_dependency(
  'cmark-cpp',
  declare_dependency(
    include_directories : include_dirs,
    link_with : libcmarkcpp,
    dependencies : cmark,
  ),
)

# Pkg-config file
pkg = import('pkgconfig')
pkg.generate(
  libcmarkcpp,
  name : 'cmark-cpp',
  description : 'C++ terminal renderer for CommonMark',
  url : 'https://github.com/NixOS/nix',
)
