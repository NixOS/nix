# libexpr.tests build file
#============================================================================

libexpr_tests_inc = [include_directories('.')]

# dependancies
#============================================================================

libexpr_tests_dep_list = [
    gtest_dep,
    rapidcheck_dep,

    libutil_dep,
    libstore_dep,
    libexpr_dep]


# src files
#============================================================================

libexpr_tests_src = files(
    'error_traces.cc',
    'json.cc',
    'primops.cc',
    'trivial.cc',
    'value/context.cc')

# build
#============================================================================

# set build args
#---------------------------------------------------
libexpr_tests_cxx_args = []

libexpr_tests_link_args = ['-lgmock']

libexpr_tests_dep = library(
    'libexpr_tests',
    sources : libexpr_tests_src,
    include_directories :  proj_inc,
    cpp_args : libexpr_tests_cxx_args,
    link_args : libexpr_tests_link_args,
    dependencies : libexpr_tests_dep_list,
    link_with : [
        libutil_tests_dep,
        libstore_tests_dep])


# build executable
#---------------------------------------------------
libexpr_tests_bin = executable(
    'libexpr-tests',
    install : false,
    include_directories : proj_inc,
    cpp_args : libexpr_tests_cxx_args,
    link_args : libexpr_tests_link_args,
    dependencies : libexpr_tests_dep_list,
    link_with : [
        libexpr_tests_dep,
        libstore_tests_dep,
        libutil_tests_dep])

test(
    'libexpr-tests',
    libexpr_tests_bin,
    suite : 'unit')
