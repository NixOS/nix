# libstore.test.machines
# ============================================================================

# TODO: currently, the test has hardcoded paths
# to the needed test data files. this isnt handled
# in this manner elsewhere in the codebase, and
# will need to be normalised.


machine_test_files = [
    'machines.valid',
    'machines.bad_format']

foreach file : machine_test_files
    configure_file(
        input : file,
        output : file,
        copy : true,
        install_dir : meson.current_build_dir())
endforeach




# build test
#---------------------------------------------------
libstore_machines_test = executable(
    'machines',
    install : false,
    sources : 'machines.cc',
    include_directories : proj_inc,
    dependencies : libstore_tests_dep_list)


test(
    'machines',
    libstore_machines_test,
    env : ['_NIX_TEST_UNIT_DATA=@0@'.format(libstore_test_data_dir)],
    protocol : 'gtest',
    suite : ['unit', 'libstore'])

