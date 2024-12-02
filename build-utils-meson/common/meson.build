# This is only conditional to work around
# https://github.com/mesonbuild/meson/issues/13293. It should be
# unconditional.
if not (host_machine.system() == 'windows' and cxx.get_id() == 'gcc')
  deps_private += dependency('threads')
endif

add_project_arguments(
  '-Wdeprecated-copy',
  '-Werror=suggest-override',
  '-Werror=switch',
  '-Werror=switch-enum',
  '-Werror=unused-result',
  '-Wignored-qualifiers',
  '-Wimplicit-fallthrough',
  '-Wno-deprecated-declarations',
  language : 'cpp',
)

if get_option('buildtype') not in ['debug']
  add_project_arguments('-O3', language : 'cpp')
endif
