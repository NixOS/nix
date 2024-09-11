# configures `scripts/nix-profile.sh.in` (and copies the original to the build directory).
# this is only needed for tests, but running it unconditionally does not hurt enough to care.
configure_file(
  input : 'nix-profile.sh.in',
  output : 'nix-profile.sh',
  configuration : {
    'localstatedir': localstatedir,
  }
)

# https://github.com/mesonbuild/meson/issues/860
configure_file(
  input : 'nix-profile.sh.in',
  output : 'nix-profile.sh.in',
  copy : true,
)

foreach rc : [ '.sh', '.fish', '-daemon.sh', '-daemon.fish' ]
  configure_file(
    input : 'nix-profile' + rc  + '.in',
    output : 'nix' + rc,
    install : true,
    install_dir : get_option('profile-dir'),
    install_mode : 'rw-r--r--',
    configuration : {
      'localstatedir': localstatedir,
    },
  )
endforeach
