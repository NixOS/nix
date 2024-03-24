# libutil.test.logging
# ============================================================================

# build test
#---------------------------------------------------
libutil_logging_test = executable(
    'libutil-logging-test',
    install : false,
    sources : 'logging.cc',
    include_directories : proj_inc,
    dependencies : libutil_tests_dep_list)

test(
    'libutil-logging-test',
    libutil_logging_test,
    protocol : 'gtest',
    suite : ['unit', 'libutil'])
