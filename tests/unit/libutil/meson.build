project('nix-util-test', 'cpp',
  version : files('.version'),
  default_options : [
    'cpp_std=c++2a',
    # TODO(Qyriad): increase the warning level
    'warning_level=1',
    'debug=true',
    'optimization=2',
    'errorlogs=true', # Please print logs for tests that fail
  ],
  meson_version : '>= 1.1',
  license : 'LGPL-2.1-or-later',
)

cxx = meson.get_compiler('cpp')

# See note in ../nix-util/meson.build
deps_private = [ ]

# See note in ../nix-util/meson.build
deps_public = [ ]

# See note in ../nix-util/meson.build
deps_other = [ ]

configdata = configuration_data()

add_project_arguments(
  # TODO(Qyriad): Yes this is how the autoconf+Make system did it.
  # It would be nice for our headers to be idempotent instead.
  '-include', 'config-util-test.h',
  # '-include', 'config-store.h',
  '-Wno-deprecated-declarations',
  '-Wimplicit-fallthrough',
  '-Werror=switch',
  '-Werror=switch-enum',
  '-Werror=unused-result',
  '-Wdeprecated-copy',
  '-Wignored-qualifiers',
  # Enable assertions in libstdc++ by default. Harmless on libc++. Benchmarked
  # at ~1% overhead in `nix search`.
  #
  # FIXME: remove when we get meson 1.4.0 which will default this to on for us:
  # https://mesonbuild.com/Release-notes-for-1-4-0.html#ndebug-setting-now-controls-c-stdlib-assertions
  '-D_GLIBCXX_ASSERTIONS=1',
  language : 'cpp',
)

# TODO rename, because it will conflict with downstream projects
configdata.set_quoted('PACKAGE_VERSION', meson.project_version())

config_h = configure_file(
  configuration : configdata,
  output : 'config-util-test.h',
)

sources = files(
  'args.cc',
  'canon-path.cc',
  'chunked-vector.cc',
  'closure.cc',
  'compression.cc',
  'config.cc',
  'file-content-address.cc',
  'git.cc',
  'hash.cc',
  'hilite.cc',
  'json-utils.cc',
  'logging.cc',
  'lru-cache.cc',
  'nix_api_util.cc',
  'pool.cc',
  'references.cc',
  'spawn.cc',
  'suggestions.cc',
  'tests.cc',
  'url.cc',
  'xml-writer.cc',
)

include_dirs = [include_directories('.')]

if host_machine.system() == 'cygwin' or host_machine.system() == 'windows'
  # Windows DLLs are stricter about symbol visibility than Unix shared
  # objects --- see https://gcc.gnu.org/wiki/Visibility for details.
  # This is a temporary sledgehammer to export everything like on Unix,
  # and not detail with this yet.
  #
  # TODO do not do this, and instead do fine-grained export annotations.
  linker_export_flags = ['-Wl,--export-all-symbols']
else
  linker_export_flags = []
endif

nix_util = dependency('nix-util')
if nix_util.type_name() == 'internal'
  # subproject sadly no good for pkg-config module
  deps_other += nix_util
else
  deps_public += nix_util
endif

nix_util_c = dependency('nix-util-c')
if nix_util_c.type_name() == 'internal'
  # subproject sadly no good for pkg-config module
  deps_other += nix_util_c
else
  deps_public += nix_util_c
endif

nix_util_test_support = dependency('nix-util-test-support')
if nix_util_test_support.type_name() == 'internal'
  # subproject sadly no good for pkg-config module
  deps_other += nix_util_test_support
else
  deps_public += nix_util_test_support
endif

rapidcheck = dependency('rapidcheck')
deps_public += rapidcheck

gtest = dependency('gtest', main : true)
deps_public += gtest

this_exe = executable(
  'nix-util-test',
  sources,
  dependencies : deps_public + deps_private + deps_other,
  include_directories : include_dirs,
  # TODO: -lrapidcheck, see ../libutil-support/build.meson
  link_args: linker_export_flags + ['-lrapidcheck'],
  # get main from gtest
  install : true,
)

test('nix-util-test', this_exe, env : ['_NIX_TEST_UNIT_DATA=' + meson.current_source_dir() + '/data'])

meson.override_dependency(meson.project_name(), declare_dependency(
  include_directories : include_dirs,
  link_with : this_exe,
  compile_args : ['-std=c++2a'],
  dependencies : [],
))
