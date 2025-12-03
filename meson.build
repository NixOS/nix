# This is just a stub project to include all the others as subprojects
# for development shell purposes

project(
  'nix-dev-shell',
  'cpp',
  version : files('.version'),
  subproject_dir : 'src',
  default_options : [
    'localstatedir=/nix/var',
  ],
  meson_version : '>= 1.1',
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
  if meson.can_run_host_binaries()
    subproject('nix-manual')
  endif
endif

# External C wrapper libraries
subproject('libutil-c')
subproject('libstore-c')
subproject('libfetchers-c')
subproject('libexpr-c')
subproject('libflake-c')
subproject('libmain-c')

asan_enabled = 'address' in get_option('b_sanitize')

# Language Bindings
if get_option('bindings') and not meson.is_cross_build() and not asan_enabled
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
if get_option('json-schema-checks')
  subproject('json-schema-checks')
endif
if get_option('kaitai-struct-checks')
  subproject('kaitai-struct-checks')
endif
