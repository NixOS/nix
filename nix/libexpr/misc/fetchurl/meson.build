# libexpr fetchurl
#============================================================================

# generate fetchurl.nix.gen.hh
#---------------------------------------------------
fetchurl_nix_gen_hh = custom_target(
  'fetchurl.nix.gen.hh',
  output : 'fetchurl.nix.gen.hh',
  input : 'fetchurl.nix',
  command : [bash, '-c', gen_rheader, 'sh', '@OUTPUT@'])
