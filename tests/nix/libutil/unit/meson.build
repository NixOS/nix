# libutil.tests
#============================================================================



# dependencies
#============================================================================

libutil_tests_dep_list = [
    gtest_dep,
    gmock_dep,
    rapidcheck_dep,

    libutil_dep,
    libutil_support_dep]


# include directories
#============================================================================

libutil_unit_test_dirs = [
    'args',
    'canon-path',
    'chunked-vector',
    'closure',
    'compression',
    'config',
    'file-content-address',
    'git',
    'hash',
    'hilite',
    'json-utils',
    'logging',
    'lru-cache',
    'misc',
    'pool',
    'references',
    'suggestions',
    'url',
    'xml-writer']

foreach dir : libutil_unit_test_dirs
    subdir(dir)
endforeach
