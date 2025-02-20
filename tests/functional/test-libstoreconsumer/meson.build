libstoreconsumer_tester = executable(
  'test-libstoreconsumer',
  'main.cc',
  cpp_args : [
    # TODO(Qyriad): Yes this is how the autoconf+Make system did it.
    # It would be nice for our headers to be idempotent instead.
    '-include', 'nix/config-util.hh',
    '-include', 'nix/config-store.hh',
  ],
  dependencies : [
    dependency('nix-store'),
  ],
  build_by_default : false,
)
