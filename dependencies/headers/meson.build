# nix check headers
#============================================================================

# check for various headers
#--------------------------------------------------
project_header_deps = [
    'sys/stat.h',
    'sys/types.h',
    'sys/dir.h',
    'sys/ndir.h',
    'dirent.h',
    'locale.h',
    'unistd.h',
    'stdint.h',
    'stdlib.h',
    'strings.h',
    'string.h',
    'inttypes.h',
    'memory.h']

foreach f : project_header_deps
  if cpp.has_header(f)
    config_h.set(
        'HAVE_@0@'.format(f.to_upper().underscorify()), 1,
        description : 'Define to 1 if you have the `<@0@>` header file..'.format(f))
  endif
endforeach


# TODO: this was carried over from the autotools buildsystem,
# but i think it may no longer be needed.

# compiler check for dirent.h
#--------------------------------------------------
dirent_h_prefix = '''
  #include <sys/types.h>
  #include <dirent.h>
'''

check_struct_dirent = meson.get_compiler('cpp').has_member(
    'struct dirent', 'd_type', prefix: dirent_h_prefix)

if ((config_h.get('HAVE_DIRENT_H') == 1) and (check_struct_dirent))
  config_h.set('HAVE_STRUCT_DIRENT_D_TYPE', 1)
endif
