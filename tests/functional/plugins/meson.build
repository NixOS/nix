libplugintest = shared_module(
  'plugintest',
  'plugintest.cc',
  cpp_args : [
    # TODO(Qyriad): Yes this is how the autoconf+Make system did it.
    # It would be nice for our headers to be idempotent instead.
    '-include', 'config-util.hh',
    '-include', 'config-store.hh',
    # '-include', 'config-fetchers.hh',
    '-include', 'config-expr.hh',
  ],
  dependencies : [
    dependency('nix-expr'),
  ],
  build_by_default : false,
)
