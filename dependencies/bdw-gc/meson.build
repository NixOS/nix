# Boehm-gc Dependency File
#============================================================================


# Look for Boehm garbage collector, an optional dependency
#--------------------------------------------------
gc_dep = dependency('bdw-gc', required: get_option('gc'))


config_h.set(
    'HAVE_BOEHMGC',
    gc_dep.found().to_int(),
    description : 'Whether to use the Boehm garbage collector.')
