# LibLzma Dependency File
#============================================================================


# Look for liblzma, a required dependency
#--------------------------------------------------
liblzma_dep = dependency('liblzma')


check_lzma_mt = cpp.has_function(
    'lzma_stream_encoder_mt',
    dependencies : liblzma_dep)

config_h.set(
    'HAVE_LZMA', 1,
    description : 'Define if the lzma library is available.')

config_h.set(
    'HAVE_LZMA_MT',
    check_lzma_mt.to_int(),
    description : 'Define if lzma_stream_encoder_mt is available.')
