# nix src build file
#============================================================================

src_dirs = [
  'libutil',
  'libstore',
  'libmain',
  'libfetchers',
  'libexpr',
  'libcmd',

  'build-remote',
  'nix-build',
  'nix-channel',
  'nix-collect-garbage',
  'nix-copy-closure',
  'nix-env',
  'nix-instantiate',
  'nix-store',
  'nix']

if sys_name == 'Darwin'
  src_dirs += 'resolve-system-dependencies'
endif

foreach dir : src_dirs
    subdir(dir)
endforeach


# libstore_config = pkg.generate(
#     libstore_lib,
#     libraries : [
#         libutil_lib],
#     version : meson.project_version(),
#     name : 'Nix',
#     subdirs : ['nix/'],
#     filebase : 'nix-store',
#     extra_cflags : '-std=c++17',
#     description : 'Nix Package Manager.')

# libmain_config = pkg.generate(
#     libmain_lib,
#     version : meson.project_version(),
#     name : 'Nix',
#     subdirs : ['nix/'],
#     filebase : 'nix-main',
#     extra_cflags : '-std=c++17',
#     description : 'Nix Package Manager.')


