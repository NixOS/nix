# libutil.test.args
# ============================================================================

# build test
#---------------------------------------------------
libutil_args_test = executable(
    'libutil-args-test',
    install : false,
    sources : 'args.cc',
    include_directories : proj_inc,
    dependencies : libutil_tests_dep_list)

test(
    'libutil-args-test',
    libutil_args_test,
    protocol : 'gtest',
    suite : ['unit', 'libutil'])
