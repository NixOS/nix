configure_file(
  input : 'completion.fish',
  output : 'nix.fish',
  install : true,
  install_dir : get_option('datadir') / 'fish' / 'vendor_completions.d',
  install_mode : 'rw-r--r--',
  copy : true,
)
