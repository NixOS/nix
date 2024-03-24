# Nix Manual Misc
#============================================================================

# configure utils.nix.gen.hh
#---------------------------------------------------
utils_nix = custom_target(
  'utils.nix.gen.hh',
  output : 'utils.nix.gen.hh',
  input : 'utils.nix',
  command : [bash, '-c', gen_rheader, 'sh', '@OUTPUT@'])
  