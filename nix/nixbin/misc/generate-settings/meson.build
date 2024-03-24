# Nix Manual Generate-Settings
#============================================================================


# configure generate-settings.nix.gen.hh
#---------------------------------------------------
generate_settings_nix = custom_target(
  'generate-settings.nix',
  output : 'generate-settings.nix.gen.hh',
  input : 'generate-settings.nix',
  command : [bash, '-c', gen_rheader, 'sh', '@OUTPUT@'])
  