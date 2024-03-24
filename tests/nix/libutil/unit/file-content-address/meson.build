# libutil.test.file-content-address
# ============================================================================

# build test
#---------------------------------------------------
libutil_file_content_address_test = executable(
    'libutil-file-content-address-test',
    install : false,
    sources : 'file-content-address.cc',
    include_directories : proj_inc,
    dependencies : libutil_tests_dep_list)

test(
    'libutil-file-content-address-test',
    libutil_file_content_address_test,
    protocol : 'gtest',
    suite : ['unit', 'libutil'])
