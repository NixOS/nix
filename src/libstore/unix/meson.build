sources += files(
  'build/child.cc',
  'build/chroot.cc',
  'build/derivation-builder.cc',
  'build/external-derivation-builder.cc',
  'build/hook-instance.cc',
  'pathlocks.cc',
  'user-lock.cc',
)

if host_machine.system() == 'linux'
  sources += files(
    'build/linux-chroot-derivation-builder.cc',
    'build/linux-derivation-builder.cc',
  )
elif host_machine.system() == 'darwin'
  sources += files(
    'build/darwin-derivation-builder.cc',
  )
else
  sources += files(
    'build/generic-unix-derivation-builder.cc',
  )
endif

subdir('include/nix/store')
