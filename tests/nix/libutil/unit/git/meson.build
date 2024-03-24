# libutil.test.git
# ============================================================================

# build test
#---------------------------------------------------
libutil_git_test = executable(
    'libutil-git-test',
    install : false,
    sources : ['git.cc'],
    include_directories : proj_inc,
    dependencies : libutil_tests_dep_list)

test(
    'libutil-git-test',
    libutil_git_test,
    env : ['_NIX_TEST_UNIT_DATA=@0@'.format(libutil_test_data_dir)],
    protocol : 'gtest',
    suite : ['unit', 'libutil'])
