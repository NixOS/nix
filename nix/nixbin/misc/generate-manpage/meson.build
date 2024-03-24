# Nix Manual Generate-Manpage
#============================================================================


# configure generate-manpage.nix.gen.hh
#---------------------------------------------------
generate_manpage_nix = custom_target(
  'generate-manpage.nix',
  output : 'generate-manpage.nix.gen.hh',
  input : 'generate-manpage.nix',
  command : [bash, '-c', gen_rheader, 'sh', '@OUTPUT@'])
  