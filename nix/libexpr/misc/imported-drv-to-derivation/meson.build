# libexpr imported-drv-to-derivation
#============================================================================

# generate imported-drv-to-derivation.nix.gen.hh
#---------------------------------------------------
imported_drv_to_derivation_nix_gen_hh = custom_target(
  'imported-drv-to-derivation.nix.gen.hh',
  output : 'imported-drv-to-derivation.nix.gen.hh',
  input : 'imported-drv-to-derivation.nix',
  command : [bash, '-c', gen_rheader, 'sh', '@OUTPUT@'])
