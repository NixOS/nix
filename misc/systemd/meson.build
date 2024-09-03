foreach config : [ 'nix-daemon.socket', 'nix-daemon.service' ]
  configure_file(
    input : config + '.in',
    output : config,
    install : true,
    install_dir : get_option('prefix') / 'lib/systemd/system',
    install_mode : 'rw-r--r--',
    configuration : {
      'storedir' : store_dir,
      'localstatedir' : localstatedir,
      'bindir' : bindir,
    },
  )
endforeach

configure_file(
  input : 'nix-daemon.conf.in',
  output : 'nix-daemon.conf',
  install : true,
  install_dir : get_option('prefix') / 'lib/tmpfiles.d',
  install_mode : 'rw-r--r--',
  configuration : {
    'localstatedir' : localstatedir,
  },
)
