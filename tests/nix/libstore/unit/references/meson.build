# libstore.test.references
# ============================================================================


# build test
#---------------------------------------------------
libstore_references_test = executable(
    'references',
    install : false,
    sources : 'references.cc',
    include_directories : proj_inc,
    dependencies : libstore_tests_dep_list)


test(
    'references',
    libstore_references_test,
    env : ['_NIX_TEST_UNIT_DATA=@0@'.format(libstore_test_data_dir)],
    protocol : 'gtest',
    suite : ['unit', 'libstore'])

