sources += files(
  'build/child.cc',
  'build/derivation-builder.cc',
  'build/hook-instance.cc',
  'pathlocks.cc',
  'user-lock.cc',
)

if host_machine.system() == 'linux'
  sources += files(
    'build/pasta.cc',
  )
endif

if host_machine.system() == 'darwin'
  sources += files(
    'build/darwin-derivation-builder.cc',
  )
endif

subdir('include/nix/store')
