# libutil.test.suggestions
# ============================================================================

# build test
#---------------------------------------------------
libutil_suggestions_test = executable(
    'libutil-suggestions-test',
    install : false,
    sources : 'suggestions.cc',
    include_directories : proj_inc,
    dependencies : libutil_tests_dep_list)

test(
    'libutil-suggestions-test',
    libutil_suggestions_test,
    protocol : 'gtest',
    suite : ['unit', 'libutil'])