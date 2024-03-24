# libutil.test.chunked-vector
# ============================================================================

# build test
#---------------------------------------------------
libutil_chunked_vector_test = executable(
    'libutil-chunked-vector-test',
    install : false,
    sources : 'chunked-vector.cc',
    include_directories : proj_inc,
    dependencies : libutil_tests_dep_list)

test(
    'libutil-chunked-vector-test',
    libutil_chunked_vector_test,
    protocol : 'gtest',
    suite : ['unit', 'libutil'])
