# nix misc
#============================================================================

nix_src_inc = [include_directories('.')]

src_dirs = [
    'build-remote',
    'nix-build',
    'nix-channel',
    'nix-collect-garbage',
    'nix-copy-closure',
    'nix-env',
    'nix-instantiate',
    'nix-store',
    'bin',
]

if sys_name == 'Darwin'
  src_dirs += 'resolve-system-dependencies'
endif

foreach dir : src_dirs
    subdir(dir)
endforeach