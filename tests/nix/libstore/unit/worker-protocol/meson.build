# libstore.test.worker-protocol
# ============================================================================


# build test
#---------------------------------------------------
libstore_worker_protocol_test = executable(
    'worker-protocol',
    install : false,
    sources : 'worker-protocol.cc',
    include_directories : proj_inc,
    dependencies : libstore_tests_dep_list)


test(
    'worker-protocol',
    libstore_worker_protocol_test,
    env : ['_NIX_TEST_UNIT_DATA=@0@'.format(libstore_test_data_dir)],
    protocol : 'gtest',
    suite : ['unit', 'libstore'])

