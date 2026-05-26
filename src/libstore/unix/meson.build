sources += files(
  'build/child.cc',
  'build/chroot.cc',
  'build/derivation-builder.cc',
  'build/hook-instance.cc',
  'pathlocks.cc',
  'user-lock.cc',
)

if host_machine.system() == 'darwin'
  sources += files('build/darwin-mach-o-rewrite.cc')
endif

subdir('include/nix/store')
