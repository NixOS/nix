# libutil.test.json-utils
# ============================================================================

# build test
#---------------------------------------------------
libutil_json_utils_test = executable(
    'libutil-json-utils-test',
    install : false,
    sources : 'json-utils.cc',
    include_directories : [
        proj_inc],
    dependencies : libutil_tests_dep_list)

test(
    'libutil-json-utils-test',
    libutil_json_utils_test,
    protocol : 'gtest',
    suite : ['unit', 'libutil'])
