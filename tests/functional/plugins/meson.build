cpp = meson.get_compiler('cpp')
dl_dep = cpp.find_library('dl', required : false)

libplugintest = shared_module(
  'plugintest',
  'plugintest.cc',
  dependencies : [
    dependency('nix-expr'),
  ],
  build_by_default : false,
)

if get_option('plugin-c-api') and not host_machine.system().startswith('windows')
  libplugintest_dlsym = shared_module(
    'plugintest_dlsym',
    'plugintest_dlsym.cc',
    dependencies : [ dl_dep ],
    build_by_default : false,
  )
endif
