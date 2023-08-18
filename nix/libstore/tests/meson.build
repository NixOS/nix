# libstore.tests build file
#============================================================================

libstore_tests_inc = [include_directories('.')]

# dependancies
#============================================================================

libstore_tests_dep_list = [
    nlohmann_dep,
    gtest_dep,
    rapidcheck_dep,

    libutil_dep,
    libstore_dep]


# src files
#============================================================================

libstore_tests_src = files(
    'derivation.cc',
    'derived-path.cc',
    'downstream-placeholder.cc',
    'machines.cc',
    'nar-info-disk-cache.cc',
    'outputs-spec.cc',
    'path.cc',
    'references.cc')


# build
#============================================================================

# set build args
#---------------------------------------------------
libstore_tests_cxx_args = []

libstore_tests_link_args = []

libstore_tests_dep = library(
    'libstore_tests',
    sources : libstore_tests_src,
    include_directories :  [
        proj_inc,
        libstore_tests_inc],
    cpp_args : libstore_tests_cxx_args,
    link_args : libstore_tests_link_args,
    dependencies : libstore_tests_dep_list,
    link_with : libutil_tests_dep)


# build executable
#---------------------------------------------------
libstore_tests_bin = executable(
    'libstore-tests',
    install : false,
    include_directories : [
        proj_inc,
        #nix_src_inc
        libstore_tests_inc],
    cpp_args : libstore_tests_cxx_args,
    link_args : libstore_tests_link_args,
    dependencies : libstore_tests_dep_list,
    link_with : [
        libstore_tests_dep,
        libutil_tests_dep])

test(
    'libstore-tests',
    libstore_tests_bin,
    suite : 'unit')
