# nix-env build file
#============================================================================

nix_env_dir = meson.current_source_dir()
nix_env_inc = [include_directories('.')]


# src files
#============================================================================

nix_env_src = files(
    join_paths(nix_env_dir, 'nix-env.cc'),
    join_paths(nix_env_dir, 'user-env.cc'))

# targets
#============================================================================
nix_env_src += custom_target(
  'buildenv.nix.gen.hh',
  output : 'buildenv.nix.gen.hh',
  input : 'buildenv.nix',
  command : [bash, '-c', gen_rheader, 'sh', '@OUTPUT@'])
