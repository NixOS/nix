# Nix lib expr build file
#============================================================================


# src files
#============================================================================

libexpr_inc += include_directories('.')

libexpr_src += files(
    'config.cc',
    'flake.cc',
    'flakeref.cc',
    'lockfile.cc')

libexpr_headers += files(
    'flake.hh',
    'flakeref.hh',
    'lockfile.hh')

# targets
#============================================================================

libexpr_src += custom_target(
  'call-flake.nix.gen.hh',
  output : 'call-flake.nix.gen.hh',
  input : 'call-flake.nix',
  command : [bash, '-c', gen_rheader, 'sh', '@OUTPUT@'])
