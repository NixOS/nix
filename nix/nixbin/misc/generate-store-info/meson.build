# Nix Manual Generate-Store-Info
#============================================================================


# configure generate-store-info.nix.gen.hh
#---------------------------------------------------
generate_store_info_nix = custom_target(
  'generate-store-info.nix',
  output : 'generate-store-info.nix.gen.hh',
  input : 'generate-store-info.nix',
  command : [bash, '-c', gen_rheader, 'sh', '@OUTPUT@'])
  