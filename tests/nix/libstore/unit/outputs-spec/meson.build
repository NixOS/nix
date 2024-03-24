# libstore.test.outputs-spec
# ============================================================================


# build test
#---------------------------------------------------
libstore_outputs_spec_test = executable(
    'outputs-spec',
    install : false,
    sources : 'outputs-spec.cc',
    include_directories : proj_inc,
    dependencies : libstore_tests_dep_list)


test(
    'outputs-spec',
    libstore_outputs_spec_test,
    env : ['_NIX_TEST_UNIT_DATA=@0@'.format(libstore_test_data_dir)],
    protocol : 'gtest',
    suite : ['unit', 'libstore'])

