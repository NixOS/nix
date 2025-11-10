requires_private = []
foreach dep : deps_private_subproject
  requires_private += dep.name()
endforeach
requires_private += deps_private

requires_public = []
foreach dep : deps_public_subproject
  requires_public += dep.name()
endforeach
requires_public += deps_public

extra_pkg_config_variables = get_variable('extra_pkg_config_variables', {})

extra_cflags = []
if not meson.project_name().endswith('-c')
  extra_cflags += [ '-std=c++23' ]
endif

import('pkgconfig').generate(
  this_library,
  filebase : meson.project_name(),
  name : 'Nix',
  description : 'Nix Package Manager',
  extra_cflags : extra_cflags,
  requires : requires_public,
  requires_private : requires_private,
  libraries_private : libraries_private,
  variables : extra_pkg_config_variables,
)

meson.override_dependency(
  meson.project_name(),
  declare_dependency(
    include_directories : include_dirs,
    link_with : this_library,
    compile_args : [ '-std=c++23' ],
    dependencies : deps_public_subproject + deps_public,
    variables : extra_pkg_config_variables,
  ),
)
