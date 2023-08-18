# nix dependencies build file
#============================================================================


# declare dependency types
#---------------------------------------------------
required_dep_dirs = [
    'archive',
    'boost',
    'brotli',
    'cpuid',
    'curl',
    'dl',
    'editline',
    # 'lowdown',
    'nlohmann-json',
    'openssl',
    'pthread',
    'rapidcheck',
    'seccomp',
    'sqlite']

optional_dep_dirs = [
    'aws-sdk-cpp',
    'bdw-gc',
    'gtest',
    'sodium']

check_dirs = [
    'functions',
    'headers']


# Build dependencies
#---------------------------------------------------
foreach dir : required_dep_dirs
    subdir(dir)
endforeach

foreach dir : optional_dep_dirs
    subdir(dir)
endforeach

foreach dir : check_dirs
    subdir(dir)
endforeach
