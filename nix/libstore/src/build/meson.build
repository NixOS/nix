# libstore.build build file
#============================================================================

libstore_inc += include_directories('.')


# src files
#============================================================================

libstore_src += files(
    'child.cc',
    'derivation-goal.cc',
    'drv-output-substitution-goal.cc',
    'entry-points.cc',
    'goal.cc',
    'hook-instance.cc',
    'local-derivation-goal.cc',
    'personality.cc',
    'substitution-goal.cc',
    'worker.cc')



libstore_headers += files(
    'child.hh',
    'derivation-goal.hh',
    'drv-output-substitution-goal.hh',
    'goal.hh',
    'hook-instance.hh',
    'local-derivation-goal.hh',
    'personality.hh',
    'substitution-goal.hh',
    'worker.hh')

libstore_data += files(
    'sandbox-defaults.sb',
    'sandbox-minimal.sb',
    'sandbox-network.sb')
