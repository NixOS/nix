# libstore.test.serve-protocol
# ============================================================================


# build test
#---------------------------------------------------
libstore_serve_protocol_test = executable(
    'serve-protocol',
    install : false,
    sources : 'serve-protocol.cc',
    include_directories : proj_inc,
    dependencies : libstore_tests_dep_list)


test(
    'serve-protocol',
    libstore_serve_protocol_test,
    env : ['_NIX_TEST_UNIT_DATA=@0@'.format(libstore_test_data_dir)],
    protocol : 'gtest',
    suite : ['unit', 'libstore'])

