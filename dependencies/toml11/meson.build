# toml Dependency File
#========================================================================


# First, attempt to find toml automatically:
toml11_dep = dependency('toml11', required : false)

# If not found, fallback to manual decleration
if not toml11_dep.found()
    toml_proj = cmake.subproject('toml11')
    gtest_dep = gtest_proj.get_variable('gtest_main_dep')
    gmock_dep = gtest_proj.get_variable('gmock_dep')
endif

