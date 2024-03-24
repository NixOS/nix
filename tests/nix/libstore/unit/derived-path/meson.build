# libstore.test.derived-path
# ============================================================================


# build test
#---------------------------------------------------
libstore_derived_path_test = executable(
    'derived-path',
    install : false,
    sources : 'derived-path.cc',
    include_directories : proj_inc,
    dependencies : libstore_tests_dep_list)


test(
    'derived-path',
    libstore_derived_path_test,
    env : ['_NIX_TEST_UNIT_DATA=@0@'.format(libstore_test_data_dir)],
    protocol : 'gtest',
    suite : ['unit', 'libstore'])

