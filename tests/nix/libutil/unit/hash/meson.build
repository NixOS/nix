# libutil.test.hash
# ============================================================================

# build test
#---------------------------------------------------
libutil_hash_test = executable(
    'libutil-hash-test',
    install : false,
    sources : 'hash.cc',
    include_directories : proj_inc,
    dependencies : libutil_tests_dep_list)

test(
    'libutil-hash-test',
    libutil_hash_test,
    protocol : 'gtest',
    suite : ['unit', 'libutil'])
