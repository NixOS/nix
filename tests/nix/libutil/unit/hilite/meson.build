# libutil.test.hilite
# ============================================================================

# build test
#---------------------------------------------------
libutil_hilite_test = executable(
    'libutil-hilite-test',
    install : false,
    sources : 'hilite.cc',
    include_directories : proj_inc,
    dependencies : libutil_tests_dep_list)

test(
    'libutil-hilite-test',
    libutil_hilite_test,
    protocol : 'gtest',
    suite : ['unit', 'libutil'])
