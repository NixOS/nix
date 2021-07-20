# Boehm-gc Dependency File
#============================================================================


# Look for Boehm garbage collector, an optional dependency
#--------------------------------------------------
if get_option('enable_gc')
    gc_dep = dependency('bdw-gc')
else
    gc_dep = dependency('', required: false)
endif


config_h.set(
    'HAVE_BOEHMGC',
    gc_dep.found().to_int(),
    description : 'Whether to use the Boehm garbage collector.')
