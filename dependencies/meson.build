# nix dependencies build file
#============================================================================


# declare dependency types
#---------------------------------------------------
required_dep_dirs = [
    'archive',
    'boost',
    'brotli',
    'bz2',
    'cpuid',
    'curl',
    'dl',
    'editline',
    'lzma',
    'lowdown',
    'nlohmann',
    'openssl',
    'seccomp',
    'sqlite',
    'pthread',
    'zlib']

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
