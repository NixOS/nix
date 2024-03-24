

# configure help-stores.md.gen.hh
#---------------------------------------------------
nix_docgen_help_stores_md = custom_target(
  'help-stores.md',
  output : 'help-stores.md',
  input : nix_index_md_in,
  command : [bash, '-c', gen_rheader, 'sh', '@OUTPUT@'])
  