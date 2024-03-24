# gtest Dependency File
#========================================================================


# check if gtest is already installed
#---------------------------------------------------
gtest_dep = dependency(
    'gtest',
    main : true,
    required : false)

gmock_dep = dependency(
    'gmock',
    main : true,
    required : false)


# If not, include the submodule
#---------------------------------------------------
if not gtest_dep.found()
    gtest_proj = subproject('gtest')
    gtest_dep = gtest_proj.get_variable('gtest_main_dep')
    gmock_dep = gtest_proj.get_variable('gmock_dep')
endif
