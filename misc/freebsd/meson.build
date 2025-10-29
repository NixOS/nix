configure_file(
  input : 'nix-daemon.in',
  output : 'nix-daemon',
  install : true,
  install_dir : get_option('prefix') / 'etc/rc.d',
  install_mode : 'rwxr-xr-x',
  configuration : {
    'bindir' : bindir,
  },
)
