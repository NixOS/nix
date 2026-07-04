sources += files(
  'build/child.cc',
  'build/chroot.cc',
  'build/derivation-builder.cc',
  'build/external-derivation-builder.cc',
  'build/hook-instance.cc',
  'macho-signature.cc',
  'pathlocks.cc',
  'user-lock.cc',
)

include_dirs += [ include_directories('build') ]

if (target_machine.system() == 'linux' or target_machine.system() == 'freebsd')
  sources += files(
    'build/chroot-derivation-builder.cc',
  )
endif

subdir('include/nix/store')
