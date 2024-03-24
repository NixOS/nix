# libstore.test.derivation
# ============================================================================


# build test
#---------------------------------------------------
libstore_content_address_test = executable(
    'derivation',
    install : false,
    sources : 'derivation.cc',
    include_directories : proj_inc,
    dependencies : libstore_tests_dep_list)


test(
    'derivation',
    libstore_content_address_test,
    env : ['_NIX_TEST_UNIT_DATA=@0@'.format(libstore_test_data_dir)],
    protocol : 'gtest',
    suite : ['unit', 'libstore'])

