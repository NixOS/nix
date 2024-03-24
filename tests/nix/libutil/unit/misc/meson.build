# libutil.test.misc
# ============================================================================

# build test
#---------------------------------------------------
libutil_misc_test = executable(
    'libutil-misc-test',
    install : false,
    sources : 'misc.cc',
    include_directories : proj_inc,
    dependencies : libutil_tests_dep_list)

test(
    'libutil-misc-test',
    libutil_misc_test,
    protocol : 'gtest',
    suite : ['unit', 'libutil'])