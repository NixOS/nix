# gtest Dependency File
#========================================================================


# check if gtest is already installed
#---------------------------------------------------
gtest_dep = dependency(
    'gtest',
    required : false)


# If not, include the submodule
#---------------------------------------------------
if not gtest_dep.found()
    gtest_proj = cmake.subproject('gtest')
    gtest_dep = gtest_proj.dependency('gtest')
endif
