# libutil.tests.support
#============================================================================

libutil_support_inc = [include_directories('.')]

# dependencies
#============================================================================

libutil_support_dep_list = [
    gtest_dep,
    rapidcheck_dep,
    # rapidcheck_gtest_dep,

    libutil_dep]


# src files
#============================================================================

libutil_support_src = files(
    join_paths('tests', 'hash.cc'))

libutil_support_headers = files(
    join_paths('tests', 'characterization.hh'),
    join_paths('tests', 'hash.hh'))


# build
#============================================================================

# set build args
#---------------------------------------------------
libutil_support_cxx_args = []

libutil_support_link_args = []

libutil_support_lib = library(
    'nixutil-support',
    sources : libutil_support_src,
    include_directories : [
        proj_inc,
        libutil_support_inc],
    cpp_args : libutil_support_cxx_args,
    link_args : libutil_support_link_args,
    dependencies : libutil_support_dep_list)



# declare dependency
#---------------------------------------------------
libutil_support_dep = declare_dependency(
    link_with : libutil_support_lib,
    include_directories : libutil_support_inc)
