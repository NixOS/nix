# nix Dependencies
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
    'lowdown',
    'libgit2',
    'nlohmann-json',
    'openssl',
    'pthread',
    'rapidcheck',
    'seccomp',
    'sqlite',
    'toml11']

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
