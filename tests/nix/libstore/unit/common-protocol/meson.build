# libstore.test.common-protocol
# ============================================================================


# build test
#---------------------------------------------------
libstore_common_protocol_test = executable(
    'common-protocol-test',
    install : false,
    sources : 'common-protocol.cc',
    include_directories : proj_inc,
    dependencies : libstore_tests_dep_list)


test(
    'common-protocol-test',
    libstore_common_protocol_test,
    protocol : 'gtest',
    suite : ['unit', 'libstore'])

