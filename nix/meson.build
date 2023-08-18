# nix build file
#============================================================================

nix_src_inc = [include_directories('.')]

src_dirs = [
  'libutil',
  'libstore',
  'libmain',
  'libfetchers',
  'libexpr',
  # 'libcmd',
  # 'nix',
]

foreach dir : src_dirs
    subdir(dir)
endforeach
