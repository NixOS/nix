# nix build file
#============================================================================

proj_inc += include_directories('.')

src_dirs = [
    'libutil',
    'libstore',
    'libmain',
    'libfetchers',
    'libexpr',
    'libcmd',
    # 'nixbin',
]

# if get_option('nix_perl').allowed()
#     src_dirs += ['perl']
# endif

foreach dir : src_dirs
    subdir(dir)
endforeach
