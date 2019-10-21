# nix src build file
#============================================================================

src_dirs = [
  'libutil',
  'libstore',
  'libmain',
  'libexpr',
  'nix',
  'resolve-system-dependencies'
]

foreach dir : src_dirs
    subdir(dir)
endforeach



libstore_config = pkg.generate(
    libstore_lib,
    libraries : [
        libutil_lib],
    version : meson.project_version(),
    name : 'Nix',
    subdirs : ['nix/'],
    filebase : 'nix-store',
    extra_cflags : '-std=c++17',
    description : 'Nix Package Manager.')

libmain_config = pkg.generate(
    libmain_lib,
    version : meson.project_version(),
    name : 'Nix',
    subdirs : ['nix/'],
    filebase : 'nix-main',
    extra_cflags : '-std=c++17',
    description : 'Nix Package Manager.')

libexpr_config = pkg.generate(
    libexpr_lib,
    libraries : [
        libstore_lib],
    version : meson.project_version(),
    name : 'Nix',
    subdirs : ['nix/'],
    filebase : 'nix-expr',
    extra_cflags : '-std=c++17',
    description : 'Nix Package Manager.')
