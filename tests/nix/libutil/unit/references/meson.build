# libutil.test.references
# ============================================================================

# build test
#---------------------------------------------------
libutil_references_test = executable(
    'libutil-references-test',
    install : false,
    sources : 'references.cc',
    include_directories : proj_inc,
    dependencies : libutil_tests_dep_list)

test(
    'libutil-references-test',
    libutil_references_test,
    protocol : 'gtest',
    suite : ['unit', 'libutil'])
