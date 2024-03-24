# libstore.test.nar-info
# ============================================================================


# build test
#---------------------------------------------------
libstore_nar_info_test = executable(
    'nar-info',
    install : false,
    sources : 'nar-info.cc',
    include_directories : proj_inc,
    dependencies : libstore_tests_dep_list)


test(
    'nar-info',
    libstore_nar_info_test,
    env : ['_NIX_TEST_UNIT_DATA=@0@'.format(libstore_test_data_dir)],
    protocol : 'gtest',
    suite : ['unit', 'libstore'])

