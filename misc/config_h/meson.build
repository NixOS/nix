# include.config_h build file
#========================================================================

# generate headers
#========================================================================

# build config.h
#---------------------------------------------------
config_h = configure_file(
    #input : 'config.h.in',
    output : 'config.h',
    configuration : config_h)

add_project_arguments('-include', 'config.h', language : 'cpp')


# install headers
#---------------------------------------------------
install_headers(
    config_h,
    install_dir : join_paths(includedir, 'nix'))


# include directories 
#========================================================================

proj_inc += include_directories('.')
