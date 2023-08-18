# get-env header
#============================================================================


env_sh = files('get-env.sh')

nix_src += custom_target(
  'get-env.sh.gen.hh',
  output : 'get-env.sh.gen.hh',
  input : env_sh,
  command : [bash, '-c', gen_rheader, 'sh', '@OUTPUT@'])

