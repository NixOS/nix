# libstore.build build file
#============================================================================

libutil_build_dir = meson.current_source_dir()
libstore_inc += include_directories('.')


# src files
#============================================================================

libstore_src += files(
    join_paths(libutil_build_dir, 'derivation-goal.cc'),
    join_paths(libutil_build_dir, 'drv-output-substitution-goal.cc'),
    join_paths(libutil_build_dir, 'entry-points.cc'),
    join_paths(libutil_build_dir, 'goal.cc'),
    join_paths(libutil_build_dir, 'hook-instance.cc'),
    join_paths(libutil_build_dir, 'local-derivation-goal.cc'),
    join_paths(libutil_build_dir, 'substitution-goal.cc'),
    join_paths(libutil_build_dir, 'worker.cc'))



libstore_headers += files(
    join_paths(libutil_build_dir, 'derivation-goal.hh'),
    join_paths(libutil_build_dir, 'drv-output-substitution-goal.hh'),
    join_paths(libutil_build_dir, 'goal.hh'),
    join_paths(libutil_build_dir, 'hook-instance.hh'),
    join_paths(libutil_build_dir, 'local-derivation-goal.hh'),
    join_paths(libutil_build_dir, 'substitution-goal.hh'),
    join_paths(libutil_build_dir, 'worker.hh'))