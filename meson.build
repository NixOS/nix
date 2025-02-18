# This is just a stub project to include all the others as subprojects
# for development shell purposes

project('nix-dev-shell', 'cpp',
  version : files('.version'),
  subproject_dir : 'src',
  default_options : [
    'localstatedir=/nix/var',
  ]
)

# Internal Libraries
subproject('libutil')
subproject('libstore')
subproject('libfetchers')
subproject('libexpr')
subproject('libflake')
subproject('libmain')
subproject('libcmd')

# Executables
subproject('nix')

# Docs
if get_option('doc-gen')
  subproject('internal-api-docs')
  subproject('external-api-docs')
  if not meson.is_cross_build()
    subproject('nix-manual')
  endif
endif

# External C wrapper libraries
subproject('libutil-c')
subproject('libstore-c')
subproject('libexpr-c')
subproject('libflake-c')
subproject('libmain-c')

# Language Bindings
if get_option('bindings') and not meson.is_cross_build()
  subproject('perl')
endif

# Testing
if get_option('unit-tests')
  subproject('libutil-test-support')
  subproject('libutil-tests')
  subproject('libstore-test-support')
  subproject('libstore-tests')
  subproject('libfetchers-tests')
  subproject('libexpr-test-support')
  subproject('libexpr-tests')
  subproject('libflake-tests')
endif
subproject('nix-functional-tests')
