# libutil.test.config
# ============================================================================

# build test
#---------------------------------------------------
libutil_config_test = executable(
    'libutil-config-test',
    install : false,
    sources : 'config.cc',
    include_directories : proj_inc,
    dependencies : libutil_tests_dep_list)

test(
    'libutil-config-test',
    libutil_config_test,
    protocol : 'gtest',
    suite : ['unit', 'libutil'])
