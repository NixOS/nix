# nix-env build file
#============================================================================

nix_env_inc = [include_directories('.')]


# src files
#============================================================================

nix_env_src = files(
    'nix-env.cc',
    'user-env.cc')

# targets
#============================================================================
nix_env_src += custom_target(
  'buildenv.nix.gen.hh',
  output : 'buildenv.nix.gen.hh',
  input : 'buildenv.nix',
  command : [bash, '-c', gen_rheader, 'sh', '@OUTPUT@'])
