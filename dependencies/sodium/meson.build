# LibSodium Dependency File
#============================================================================

# FIXME !! sodium is seemingly a required dependency for building, but 
# it isn't listed anywhere on documentation that i could find. dependency
# object needs to be added to meson.

# look for libsodium, a required dependency
#--------------------------------------------------
libsodium_dep = cpp.find_library('sodium')


config_h.set(
    'HAVE_SODIUM', 1,
    description : 'Whether to use libsodium for cryptography.')
