# libutil.test.pool
# ============================================================================

# build test
#---------------------------------------------------
libutil_pool_test = executable(
    'libutil-pool-test',
    install : false,
    sources : 'pool.cc',
    include_directories : proj_inc,
    dependencies : libutil_tests_dep_list)

test(
    'libutil-pool-test',
    libutil_pool_test,
    protocol : 'gtest',
    suite : ['unit', 'libutil'])
