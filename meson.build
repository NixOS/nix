# This is just a stub project to include all the others as subprojects
# for development shell purposes

project('nix-dev-shell', 'cpp',
  version : files('.version'),
  subproject_dir : 'src',
)

subproject('libutil')
subproject('libstore')
subproject('libfetchers')
subproject('libexpr')
subproject('libflake')

# Docs
#subproject('internal-api-docs')
#subproject('external-api-docs')

# C wrappers
subproject('libutil-c')
subproject('libstore-c')
subproject('libexpr-c')

# Language Bindings
subproject('perl')

# Testing
subproject('libutil-test-support')
subproject('libutil-test')
subproject('libstore-test-support')
subproject('libstore-test')
subproject('libfetchers-test')
subproject('libexpr-test-support')
subproject('libexpr-test')
subproject('libflake-test')
