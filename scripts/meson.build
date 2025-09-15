configure_file(
  input : 'nix-profile.sh.in',
  output : 'nix-profile.sh',
  configuration : {
    'localstatedir' : localstatedir,
  },
)

foreach rc : [ '.sh', '.fish', '-daemon.sh', '-daemon.fish' ]
  configure_file(
    input : 'nix-profile' + rc + '.in',
    output : 'nix' + rc,
    install : true,
    install_dir : get_option('profile-dir'),
    install_mode : 'rw-r--r--',
    configuration : {
      'localstatedir' : localstatedir,
    },
  )
endforeach
