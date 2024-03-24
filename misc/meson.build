# Nix Misc
#========================================================================

include_dir = meson.current_source_dir()
proj_inc += include_directories('.')

# include directories
#========================================================================


include_dirs = [
    'config_h',
    'precompiled_headers_h',
    'macros']


foreach dir : include_dirs
    subdir(dir)
endforeach
