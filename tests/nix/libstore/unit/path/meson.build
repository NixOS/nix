# libstore.test.path
# ============================================================================


# build test
#---------------------------------------------------
libstore_path_test = executable(
    'path',
    install : false,
    sources : 'path.cc',
    include_directories : proj_inc,
    dependencies : libstore_tests_dep_list)


test(
    'path',
    libstore_path_test,
    env : ['_NIX_TEST_UNIT_DATA=@0@'.format(libstore_test_data_dir)],
    protocol : 'gtest',
    suite : ['unit', 'libstore'])

