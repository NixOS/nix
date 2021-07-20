# libstore.builtins build file
#============================================================================

libutil_builtins_dir = meson.current_source_dir()
libstore_inc += include_directories('.')


# src files
#============================================================================

libstore_src += files(
    join_paths(libutil_builtins_dir, 'buildenv.cc'),
    join_paths(libutil_builtins_dir, 'fetchurl.cc'),
    join_paths(libutil_builtins_dir, 'unpack-channel.cc'))


libstore_headers += files(
    join_paths(libutil_builtins_dir, 'buildenv.hh'))
