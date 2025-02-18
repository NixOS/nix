project('nix-main', 'cpp',
  version : files('.version'),
  default_options : [
    'cpp_std=c++2a',
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

deps_private_maybe_subproject = [
]
deps_public_maybe_subproject = [
  dependency('nix-util'),
  dependency('nix-store'),
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
  description: 'Optionally used for buffering on standard error'
)

config_h = configure_file(
  configuration : configdata,
  output : 'config-main.hh',
)

add_project_arguments(
  # TODO(Qyriad): Yes this is how the autoconf+Make system did it.
  # It would be nice for our headers to be idempotent instead.
  '-include', 'config-util.hh',
  '-include', 'config-store.hh',
  '-include', 'config-main.hh',
  language : 'cpp',
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

include_dirs = [include_directories('.')]

headers = [config_h] + files(
  'common-args.hh',
  'loggers.hh',
  'plugin.hh',
  'progress-bar.hh',
  'shared.hh',
)

this_library = library(
  'nixmain',
  sources,
  dependencies : deps_public + deps_private + deps_other,
  prelink : true, # For C++ static initializers
  install : true,
)

install_headers(headers, subdir : 'nix', preserve_path : true)

libraries_private = []

subdir('nix-meson-build-support/export')
