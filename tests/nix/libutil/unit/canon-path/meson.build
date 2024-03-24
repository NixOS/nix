# libutil.test.canon-path
# ============================================================================

# build test
#---------------------------------------------------
libutil_canon_path_test = executable(
    'libutil-canon-path-test',
    install : false,
    sources : 'canon-path.cc',
    include_directories : proj_inc,
    dependencies : libutil_tests_dep_list)

test(
    'libutil-canon-path-test',
    libutil_canon_path_test,
    protocol : 'gtest',
    suite : ['unit', 'libutil'])
