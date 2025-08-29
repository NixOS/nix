# Nix-Perl Scripts
#============================================================================



# Sources
#-------------------------------------------------

nix_perl_store_xs = files('Store.xs')

nix_perl_scripts = files(
  'CopyClosure.pm',
  'Manifest.pm',
  'SSH.pm',
  'Store.pm',
  'Utils.pm',
)

nix_perl_scripts_copy_tgts = []
foreach f : nix_perl_scripts
  nix_perl_scripts_copy_tgts += fs.copyfile(f)
endforeach


# Targets
#---------------------------------------------------

nix_perl_scripts += configure_file(
  output : 'Config.pm',
  input : 'Config.pm.in',
  configuration : nix_perl_conf,
)

nix_perl_store_cc = custom_target(
  'Store.cc',
  output : 'Store.cc',
  input : nix_perl_store_xs,
  command : [ xsubpp, '@INPUT@', '-output', '@OUTPUT@' ],
)

# Build Nix::Store Library
#-------------------------------------------------
nix_perl_store_lib = library(
  'Store',
  sources : nix_perl_store_cc,
  name_prefix : '',
  prelink : true, # For C++ static initializers
  install : true,
  install_mode : 'rwxr-xr-x',
  install_dir : join_paths(nix_perl_install_dir, 'auto', 'Nix', 'Store'),
  dependencies : nix_perl_store_dep_list,
)


# Install Scripts
#---------------------------------------------------
install_data(
  nix_perl_scripts,
  install_mode : 'rw-r--r--',
  install_dir : join_paths(nix_perl_install_dir, 'Nix'),
)
