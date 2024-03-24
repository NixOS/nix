# libstore.test.path-info
# ============================================================================


# build test
#---------------------------------------------------
libstore_path_info_test = executable(
    'path-info',
    install : false,
    sources : 'path-info.cc',
    include_directories : proj_inc,
    dependencies : libstore_tests_dep_list)


test(
    'path-info',
    libstore_path_info_test,
    env : ['_NIX_TEST_UNIT_DATA=@0@'.format(libstore_test_data_dir)],
    protocol : 'gtest',
    suite : ['unit', 'libstore'])

