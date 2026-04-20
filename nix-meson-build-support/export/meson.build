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
fs = import('fs')
plugin_c_api_enabled = get_variable('plugin_c_api_enabled', false)

if plugin_c_api_enabled
  this_libraries = get_variable('this_libraries')
  this_library = this_libraries.get_shared_lib()
  this_static_library = this_libraries.get_static_lib()
else
  this_library = get_variable('this_library')
endif

extra_cflags = []
if not meson.project_name().endswith('-c')
  extra_cflags += [ '-std=c++23' ]
endif

whole_archive_dep_name = meson.project_name() + '-whole-archive'
installed_whole_archive_link_arg = ''
if plugin_c_api_enabled and host_machine.system() == 'darwin'
  installed_whole_archive_link_arg = '-Wl,-force_load,${libdir}/' + fs.name(this_static_library.full_path())
elif plugin_c_api_enabled and not host_machine.system().startswith('windows')
  installed_whole_archive_link_arg = '-Wl,--whole-archive,${libdir}/' + fs.name(this_static_library.full_path()) + ',--no-whole-archive'
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

if installed_whole_archive_link_arg != ''
  import('pkgconfig').generate(
    filebase : whole_archive_dep_name,
    name : 'Nix whole-archive static dependency',
    description : 'Force-load static Nix C API archive into the host executable',
    extra_cflags : extra_cflags,
    requires : requires_public,
    requires_private : requires_private,
    libraries : [ installed_whole_archive_link_arg ],
    variables : extra_pkg_config_variables,
  )
endif

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

if installed_whole_archive_link_arg != ''
  meson.override_dependency(
    whole_archive_dep_name,
    declare_dependency(
      include_directories : include_dirs,
      link_whole : [ this_static_library ],
      compile_args : [ '-std=c++23' ],
      dependencies : deps_public_subproject + deps_public,
      variables : extra_pkg_config_variables,
    ),
  )
endif
