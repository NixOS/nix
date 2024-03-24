

# configure help-stores.md.gen.hh
#---------------------------------------------------
nix_docgen_profile_md = custom_target(
  'profile.md.gen.hh',
  output : 'profile.md.gen.hh',
  input : nix_profile_md,
  command : [bash, '-c', gen_rheader, 'sh', '@OUTPUT@'])
  