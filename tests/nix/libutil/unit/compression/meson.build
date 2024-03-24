# libutil.test.compression
# ============================================================================

# build test
#---------------------------------------------------
libutil_compression_test = executable(
    'libutil-compression-test',
    install : false,
    sources : 'compression.cc',
    include_directories : proj_inc,
    dependencies : libutil_tests_dep_list)

test(
    'libutil-compression-test',
    libutil_compression_test,
    protocol : 'gtest',
    suite : ['unit', 'libutil'])
