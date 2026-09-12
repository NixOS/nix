configure_file(
  input : 'nix-daemon.in',
  output : 'nix-daemon',
  install : true,
  install_dir : get_option('prefix') / 'lib/openrc',
  install_mode : 'rw-r--r--',
  configuration : {
    'storedir' : store_dir,
    'localstatedir' : localstatedir,
    'bindir' : bindir,
  },
)
