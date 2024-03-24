# libstore.tests
#============================================================================

libstore_tests_inc = [include_directories('.')]

# dependencies
#============================================================================

libstore_tests_dep_list = [
    aws_sdk_cpp_dep,
    libcurl_dep,
    libdl_dep,
    libseccomp_dep,
    libsodium_dep,
    nlohmann_dep,
    pthread_dep,
    sqlite3_dep,




    gtest_dep,
    gmock_dep,
    rapidcheck_dep,

    libutil_support_dep,
    libutil_dep,
    libstore_support_dep,
    libstore_dep]


# include directories
#============================================================================

libstore_unit_test_dirs = [
    'common-protocol',
    'content-address',
    # 'derivation',
    # 'derived-path',
    # 'downstream-placeholder',
    # 'machines',
    # 'nar-info',
    # 'nar-info-disk-cache',
    # 'outputs-spec',
    # 'path',
    # 'path-info',
    # 'references',
    # 'serve-protocol',
    # 'worker-protocol'
    ]

foreach dir : libstore_unit_test_dirs
    subdir(dir)
endforeach

