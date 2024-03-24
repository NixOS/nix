# get-env header
#============================================================================


env_sh = files('get-env.sh')

get_env_hh = custom_target(
  'get-env.sh.gen.hh',
  output : 'get-env.sh.gen.hh',
  input : env_sh,
  command : [bash, '-c', gen_rheader, 'sh', '@OUTPUT@'])

