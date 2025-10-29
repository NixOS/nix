project(
  'nix-main',
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

configdata = configuration_data()

deps_private_maybe_subproject = []
deps_public_maybe_subproject = [
  dependency('nix-util'),
  dependency('nix-store'),
  # FIXME: This is only here for the NIX_USE_BOEHMGC macro dependency
  #        Removing nix-expr will make the build more concurrent and is
  #        architecturally nice, perhaps.
  dependency('nix-expr'),
]
subdir('nix-meson-build-support/subprojects')

pubsetbuf_test = '''
#include <iostream>

using namespace std;

char buf[1024];

int main() {
  cerr.rdbuf()->pubsetbuf(buf, sizeof(buf));
}
'''

configdata.set(
  'HAVE_PUBSETBUF',
  cxx.compiles(pubsetbuf_test).to_int(),
  description : 'Optionally used for buffering on standard error',
)

config_priv_h = configure_file(
  configuration : configdata,
  output : 'main-config-private.hh',
)

subdir('nix-meson-build-support/common')

sources = files(
  'common-args.cc',
  'loggers.cc',
  'plugin.cc',
  'progress-bar.cc',
  'shared.cc',
)

if host_machine.system() != 'windows'
  sources += files(
    'unix/stack.cc',
  )
endif

subdir('include/nix/main')

subdir('nix-meson-build-support/export-all-symbols')
subdir('nix-meson-build-support/windows-version')

this_library = library(
  'nixmain',
  sources,
  config_priv_h,
  soversion : nix_soversion,
  dependencies : deps_public + deps_private + deps_other,
  include_directories : include_dirs,
  link_args : linker_export_flags,
  prelink : true, # For C++ static initializers
  install : true,
)

install_headers(headers, subdir : 'nix/main', preserve_path : true)

libraries_private = []

subdir('nix-meson-build-support/export')
