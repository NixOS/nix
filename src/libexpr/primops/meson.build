# Nix lib expr build file
#============================================================================


libexpr_priomops_dir = meson.current_source_dir()

# src files
#============================================================================

libexpr_inc += include_directories('.')

libexpr_src += files(
    join_paths(libexpr_priomops_dir, 'context.cc'),
    join_paths(libexpr_priomops_dir, 'fetchMercurial.cc'),
    join_paths(libexpr_priomops_dir, 'fetchTree.cc'),
    join_paths(libexpr_priomops_dir, 'fromTOML.cc'))


# targets
#============================================================================

libexpr_src += custom_target(
  'derivation.nix.gen.hh',
  output : 'derivation.nix.gen.hh',
  input : 'derivation.nix',
  command : [bash, '-c', gen_rheader, 'sh', '@OUTPUT@'])
