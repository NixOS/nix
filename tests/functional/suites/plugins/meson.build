add_languages('cpp', native : false)
cpp = meson.get_compiler('cpp')
dl_dep = cpp.find_library('dl', required : false)

plugin_test_deps = []
plugin_tests = []

libplugintest = shared_module(
  'plugintest',
  'src/plugintest.cc',
  dependencies : [
    dependency('nix-expr'),
  ],
  build_by_default : false,
)

plugin_tests += [ 'register-setting-and-primop.sh' ]
plugin_test_deps += [ libplugintest ]

if get_option('plugin-c-api') and not host_machine.system().startswith('windows')
  libplugintest_dlsym = shared_module(
    'plugintest_dlsym',
    'src/plugintest_dlsym.cc',
    dependencies : [ dl_dep ],
    build_by_default : false,
  )

  plugin_tests += [ 'export-c-api.sh' ]
  plugin_test_deps += [ libplugintest_dlsym ]
endif

plugin_test_conf = configuration_data()
plugin_test_conf.set('PLUGINS_BUILD_DIR', meson.current_build_dir())

plugin_test_common = configure_file(
  input : 'common.sh.in',
  output : 'common.sh',
  configuration : plugin_test_conf,
)

suites += {
  'name' : 'plugins',
  'deps' : plugin_test_deps,
  'tests' : plugin_tests,
  'workdir' : meson.current_source_dir(),
  'env' : {'BASH_ENV' : plugin_test_common.full_path()},
}
