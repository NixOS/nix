# rapidcheck Dependency File
#========================================================================



# Find rapidcheck using the provided option
rapidcheck_dir = get_option('rapidcheck_dir')

rapidcheck_inc = [
    include_directories(rapidcheck_dir),
    include_directories(join_paths(rapidcheck_dir, 'include')),
    include_directories(join_paths(rapidcheck_dir, 'extras', 'gtest', 'include'))]

rapidcheck_dep = declare_dependency(
    dependencies : cpp.find_library('rapidcheck'),
    include_directories : rapidcheck_inc)
