requires_private = []
foreach dep : deps_private_subproject
  requires_private  += dep.name()
endforeach
requires_private += deps_private

requires_public  = []
foreach dep : deps_public_subproject
  requires_public  += dep.name()
endforeach
requires_public += deps_public

import('pkgconfig').generate(
  this_library,
  filebase : meson.project_name(),
  name : 'Nix',
  description : 'Nix Package Manager',
  subdirs : ['nix'],
  extra_cflags : ['-std=c++2a'],
  requires : requires_public,
  requires_private : requires_private,
  libraries_private : libraries_private,
)

meson.override_dependency(meson.project_name(), declare_dependency(
  include_directories : include_dirs,
  link_with : this_library,
  compile_args : ['-std=c++2a'],
  dependencies : deps_public_subproject + deps_public,
))
