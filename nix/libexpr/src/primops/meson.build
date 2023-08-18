# Nix lib expr build file
#============================================================================


# src files
#============================================================================

libexpr_inc += include_directories('.')

libexpr_src += files(
    'context.cc',
    'fetchClosure.cc',
    'fetchMercurial.cc',
    'fetchTree.cc',
    'fromTOML.cc')

# targets
#============================================================================

libexpr_src += custom_target(
  'derivation.nix.gen.hh',
  output : 'derivation.nix.gen.hh',
  input : 'derivation.nix',
  command : [bash, '-c', gen_rheader, 'sh', '@OUTPUT@'])
