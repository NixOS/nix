# libutil.tests build file
#============================================================================

libutil_tests_inc = [include_directories('.')]

# dependancies
#============================================================================

libutil_tests_dep_list = [
    gtest_dep,
    rapidcheck_dep,

    libutil_dep]


# src files
#============================================================================

libutil_tests_src = files(
        'canon-path.cc',
        'chunked-vector.cc',
        'closure.cc',
        'compression.cc',
        'config.cc',
        'git.cc',
        'thash.cc',
        'hilite.cc',
        'logging.cc',
        'lru-cache.cc',
        'main.cc',
        'pool.cc',
        'references.cc',
        'suggestions.cc',
        'tests.cc',
        'url.cc',
        'xml-writer.cc')

libutil_tests_h = files(
    'thash.hh')


# build
#============================================================================

# set build args
#---------------------------------------------------
libutil_tests_cxx_args = []

libutil_tests_link_args = []

libutil_tests_dep = library(
    'libutil_tests',
    sources : libutil_tests_src,
    include_directories : [
        proj_inc,
        libutil_inc],
    cpp_args : libutil_tests_cxx_args,
    link_args : libutil_tests_link_args,
    dependencies : libutil_tests_dep_list)


# build executable
#---------------------------------------------------
libutil_tests_bin = executable(
    'libutil-tests',
    install : false,
    include_directories : proj_inc,
    cpp_args : libutil_tests_cxx_args,
    link_args : libutil_tests_link_args,
    dependencies : libutil_tests_dep_list,
    link_with : libutil_tests_dep)

test(
    'libutil-tests',
    libutil_tests_bin,
    suite : 'unit')