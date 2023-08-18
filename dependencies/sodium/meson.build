# LibSodium Dependency File
#============================================================================

# FIXME !! sodium is seemingly a required dependency for building, but 
# it isn't listed anywhere on documentation that i could find. dependency
# object needs to be added to meson.
#
# 0df69d96e02ce4c9e17bd33333c5d78313341dd3 made it required but docs were not
# updated.

# look for libsodium, a required dependency
#--------------------------------------------------
libsodium_dep = cpp.find_library('sodium')


# set config variable(s)
#--------------------------------------------------
config_h.set(
    'HAVE_SODIUM', 1,
    description : 'Whether to use libsodium for cryptography.')
