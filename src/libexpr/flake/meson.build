# Nix lib expr build file
#============================================================================


libexpr_priomops_dir = meson.current_source_dir()

# src files
#============================================================================

libexpr_inc += include_directories('.')

libexpr_src += files(
    join_paths(libexpr_priomops_dir, 'config.cc'),
    join_paths(libexpr_priomops_dir, 'flake.cc'),
    join_paths(libexpr_priomops_dir, 'flakeref.cc'),
    join_paths(libexpr_priomops_dir, 'lockfile.cc'))

libexpr_headers += files(
    join_paths(libexpr_priomops_dir, 'flake.hh'),
    join_paths(libexpr_priomops_dir, 'flakeref.hh'),
    join_paths(libexpr_priomops_dir, 'lockfile.hh'))


# targets
#============================================================================

libexpr_src += custom_target(
  'call-flake.nix.gen.hh',
  output : 'call-flake.nix.gen.hh',
  input : 'call-flake.nix',
  command : [bash, '-c', gen_rheader, 'sh', '@OUTPUT@'])
