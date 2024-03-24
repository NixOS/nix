# libstore.tests.support
#============================================================================

libstore_support_inc = [include_directories('.')]

# dependencies
#============================================================================

libstore_support_dep_list = [
    gtest_dep,
    rapidcheck_dep,

    libutil_dep,
    libutil_support_dep,
    libstore_dep]


# src files
#============================================================================

libstore_support_src = files(
    join_paths('tests', 'derived-path.cc'),
    join_paths('tests', 'outputs-spec.cc'),
    join_paths('tests', 'path.cc'))

libstore_support_headers = files(
    join_paths('tests', 'derived-path.hh'),
    join_paths('tests', 'outputs-spec.hh'),
    join_paths('tests', 'path.hh'),
    join_paths('tests', 'protocol.hh'))


# build
#============================================================================

# set build args
#---------------------------------------------------
libstore_support_cxx_args = []

libstore_support_link_args = []

libstore_support_lib = library(
    'nixstore-support',
    sources : libstore_support_src,
    include_directories : [
        proj_inc,
        libstore_support_inc],
    cpp_args : libstore_support_cxx_args,
    link_args : libstore_support_link_args,
    dependencies : libstore_support_dep_list)



# declare dependency
#---------------------------------------------------
libstore_support_dep = declare_dependency(
    link_with : libstore_support_lib,
    include_directories : libstore_support_inc)
