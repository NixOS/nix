# LibBrotli Dependency File
#============================================================================


# Look for libbrotli{enc,dec}, a required dependency
#--------------------------------------------------
libbrotli_dep = declare_dependency(
    dependencies: [
        dependency('libbrotlienc'),
        dependency('libbrotlidec')])


# set config variable(s)
#--------------------------------------------------
config_h.set(
    'HAVE_BROTLI', 1,
    description : 'Define if the brotli library is available.')
