configure_file(
  input : 'nix-profile.sh.in',
  output : 'nix-profile.sh',
  configuration : {
    'localstatedir' : localstatedir,
  },
)

foreach rc : [ '.sh', '-daemon.sh' ]
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

foreach rc : [ '.fish', '-daemon.fish' ]
  configure_file(
    input : 'nix-profile' + rc + '.in',
    output : 'nix' + rc,
    install : true,
    install_dir : get_option('datadir') / 'fish' / 'vendor_conf.d',
    install_mode : 'rw-r--r--',
    configuration : {
      'localstatedir' : localstatedir,
    },
  )
endforeach
