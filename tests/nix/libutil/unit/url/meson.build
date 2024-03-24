# libutil.test.url
# ============================================================================

# build test
#---------------------------------------------------
libutil_url_test = executable(
    'libutil-url-test',
    install : false,
    sources : 'url.cc',
    include_directories : proj_inc,
    dependencies : libutil_tests_dep_list)

test(
    'libutil-url-test',
    libutil_url_test,
    protocol : 'gtest',
    suite : ['unit', 'libutil'])