# libstore.test.content-addresss
# ============================================================================


# build test
#---------------------------------------------------
libstore_content_address_test = executable(
    'content-address-test',
    install : false,
    sources : 'content-address.cc',
    include_directories : proj_inc,
    dependencies : libstore_tests_dep_list)


test(
    'content-address-test',
    libstore_content_address_test,
    env : ['_NIX_TEST_UNIT_DATA=@0@'.format(libstore_test_data_dir)],
    protocol : 'gtest',
    suite : ['unit', 'libstore'])

