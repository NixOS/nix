# libstore.test.downstream-placeholder
# ============================================================================


# build test
#---------------------------------------------------
libstore_downstream_placeholder_test = executable(
    'downstream-placeholder',
    install : false,
    sources : 'downstream-placeholder.cc',
    include_directories : proj_inc,
    dependencies : libstore_tests_dep_list)


test(
    'downstream-placeholder',
    libstore_downstream_placeholder_test,
    env : ['_NIX_TEST_UNIT_DATA=@0@'.format(libstore_test_data_dir)],
    protocol : 'gtest',
    suite : ['unit', 'libstore'])

