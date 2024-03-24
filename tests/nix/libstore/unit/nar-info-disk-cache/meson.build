# libstore.test.nar-info-disk-cache
# ============================================================================


# build test
#---------------------------------------------------
libstore_nar_info_disk_cache_test = executable(
    'nar-info-disk-cache',
    install : false,
    sources : 'nar-info-disk-cache.cc',
    include_directories : proj_inc,
    dependencies : libstore_tests_dep_list)


test(
    'nar-info-disk-cache',
    libstore_nar_info_disk_cache_test,
    env : ['_NIX_TEST_UNIT_DATA=@0@'.format(libstore_test_data_dir)],
    protocol : 'gtest',
    suite : ['unit', 'libstore'])

