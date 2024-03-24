# rapidcheck Dependency File
#========================================================================


# First, attempt to find rapidcheck automatically:
rapidcheck_dep = dependency('rapidcheck', required : false)

# If not found, fallback to manual decleration
if not rapidcheck_dep.found()

    # fetch rapidcheck location
    rapidcheck_dir = get_option('rapidcheck_dir')

    # include rapidcheck directories
    rapidcheck_inc = [
        # include_directories(join_paths(rapidcheck_dir, 'extras', 'gtest', 'include')),
        include_directories(get_option('rapidcheck_headers'))]

    # declare rapidcheck dependency
    rapidcheck_dep = declare_dependency(
        dependencies : cpp.find_library('rapidcheck'),
        include_directories : rapidcheck_inc)
endif

