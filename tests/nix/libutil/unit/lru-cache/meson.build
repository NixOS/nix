# libutil.test.lru-cache
# ============================================================================

# build test
#---------------------------------------------------
libutil_lru_cache_test = executable(
    'libutil-lru-cache-test',
    install : false,
    sources : 'lru-cache.cc',
    include_directories : proj_inc,
    dependencies : libutil_tests_dep_list)

test(
    'libutil-lru-cache-test',
    libutil_lru_cache_test,
    protocol : 'gtest',
    suite : ['unit', 'libutil'])
