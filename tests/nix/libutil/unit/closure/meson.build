# libutil.test.closure
# ============================================================================

# build test
#---------------------------------------------------
libutil_closure_test = executable(
    'libutil-closure-test',
    install : false,
    sources : 'closure.cc',
    include_directories : [
        proj_inc],
    dependencies : libutil_tests_dep_list)

test(
    'libutil-closure-test',
    libutil_closure_test,
    protocol : 'gtest',
    suite : ['unit', 'libutil'])
