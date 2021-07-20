# libutil.tests build file
#============================================================================

libutil_tests_dir = meson.current_source_dir()
libutil_tests_inc = [include_directories('.')]

# dependancies
#============================================================================

libutil_tests_dep_list = [
    gtest_dep,

    libutil_dep]


# src files
#============================================================================

libutil_tests_src = files(
    join_paths(libutil_tests_dir, 'compression.cc'),
    join_paths(libutil_tests_dir, 'config.cc'),
    join_paths(libutil_tests_dir, 'hash.cc'),
    join_paths(libutil_tests_dir, 'json.cc'),
    join_paths(libutil_tests_dir, 'logging.cc'),
    join_paths(libutil_tests_dir, 'lru-cache.cc'),
    join_paths(libutil_tests_dir, 'main.cc'),
    join_paths(libutil_tests_dir, 'pool.cc'),
    join_paths(libutil_tests_dir, 'tests.cc'),
    join_paths(libutil_tests_dir, 'url.cc'),
    join_paths(libutil_tests_dir, 'xml-writer.cc'))


# build
#============================================================================

# set build args
#---------------------------------------------------
libutil_tests_cxx_args = []

libutil_tests_link_args = []


# build executable
#---------------------------------------------------
libutil_tests_bin = executable(
    'libutil-tests',
    sources : libutil_tests_src,
    install : false,
    include_directories : proj_inc,
    cpp_args : libutil_tests_cxx_args,
    link_args : libutil_tests_link_args,
    dependencies : libutil_tests_dep_list)

test('libutil-tests', libutil_tests_bin)
